#include "LicenseManager.h"

namespace taiji
{
// 混淆用的固定密钥（仅增加静态分析的门槛，不是真加密；license_key 的权威校验在服务器）
constexpr char kObfKey[] = "OHAUDIO-FUSIONEQ-LICENSE-2026";
constexpr int  kObfKeyLen = (int) (sizeof (kObfKey) - 1);

// 离线信任上限：距上次联网校验超过这么多毫秒才在启动时再 verify（API.md 建议 7–30 天）
constexpr juce::int64 kVerifyIntervalMs = 7LL * 24 * 60 * 60 * 1000;

// 归一化产品 ID：转小写后只留字母数字（顺手吃掉空格 / 连字符）。
// 背景：服务端 PRODUCTS 的 key 是 "Fusion EQ"（带空格），而构建时注入的
// OHAUDIO_PRODUCT_ID 是 "FusionEQ"（无空格）。两端差一个空格时，下面的精确
// 匹配会把合法凭证当「别的产品」直接丢弃，表现为「激活成功闪一下又退回」。
// 归一化后 "Fusion EQ" / "FusionEQ" / "fusioneq" 都视作同一款，
// 与服务端 resolve_product_id 的口径对齐，彻底消除空格/大小写引发的误判。
juce::String normalizeProductId (const juce::String& raw)
{
    return raw.toLowerCase().retainCharacters ("abcdefghijklmnopqrstuvwxyz0123456789");
}

// 对字符串的原始 UTF-8 字节做 XOR，再 base64。直接动字节，避开 String 的码点语义。
juce::String LicenseManager::scramble (const juce::String& s)
{
    juce::MemoryBlock mb;
    mb.setSize (s.getNumBytesAsUTF8() + 1);
    s.copyToUTF8 (static_cast<char*> (mb.getData()), static_cast<int> (mb.getSize()));

    const size_t n = (size_t) s.getNumBytesAsUTF8();
    auto* p = static_cast<unsigned char*> (mb.getData());
    for (size_t i = 0; i < n; ++i)
        p[i] ^= (unsigned char) kObfKey[i % kObfKeyLen];

    juce::MemoryOutputStream mos;
    juce::Base64::convertToBase64 (mos, p, n);
    return mos.toString();
}

juce::String LicenseManager::unscramble (const juce::String& b64)
{
    juce::MemoryOutputStream mos;
    if (! juce::Base64::convertFromBase64 (mos, b64))
        return {};

    juce::MemoryBlock mb (mos.getData(), mos.getDataSize());
    auto* p = static_cast<unsigned char*> (mb.getData());
    for (size_t i = 0; i < mb.getSize(); ++i)
        p[i] ^= (unsigned char) kObfKey[i % kObfKeyLen];

    return juce::String::fromUTF8 (reinterpret_cast<const char*> (p), (int) mb.getSize());
}

const juce::String& LicenseManager::apiBase()
{
    static const juce::String url ("https://ohaudio.cn/account/api.php");
    return url;
}

juce::String LicenseManager::version()
{
    return product::version(); // CMake 的 OHAUDIO_PRODUCT_VERSION 注入
}

juce::File LicenseManager::licenseDir()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("OHAudio");
}

juce::File LicenseManager::licenseFile()
{
    return licenseDir().getChildFile (product::licenseFileName());
}

juce::File LicenseManager::machineIdFile()
{
    return licenseDir().getChildFile (product::machineIdFileName());
}

juce::String LicenseManager::getMachineId()
{
    auto f = machineIdFile();
    if (f.existsAsFile())
    {
        auto s = f.loadFileAsString().trim();
        if (s.isNotEmpty())
            return s;
    }

    // 兼容 1.0.9 之前的 taiji.mid：把同一个 ID 迁到机器级文件名。
    // 绝不重新生成 —— 否则服务器会把这台机器认成一台新设备，白占一个名额。
    auto legacy = licenseDir().getChildFile (product::legacyMachineIdFileName());
    if (legacy.existsAsFile())
    {
        auto s = legacy.loadFileAsString().trim();
        if (s.isNotEmpty())
        {
            licenseDir().createDirectory();
            f.replaceWithText (s);
            return s;
        }
    }

    auto id = juce::Uuid().toString();
    auto dir = licenseDir();
    dir.createDirectory();
    f.replaceWithText (id);
    // Windows 上 JUCE 对文件权限控制有限，做到最好即可（API.md 第 5 节）
    f.setReadOnly (false);
    return id;
}

bool LicenseManager::hasLicense()
{
    return licenseFile().existsAsFile();
}

juce::var LicenseManager::readLicense()
{
    auto f = licenseFile();
    if (! f.existsAsFile())
        return {};

    auto raw = f.loadFileAsString().trim();
    if (raw.isEmpty())
        return {};

    auto json = unscramble (raw);
    if (json.isEmpty())
        return {};

    auto parsed = juce::JSON::parse (json);
    if (parsed.getDynamicObject() == nullptr)
        return {};

    // 别的产品凭证（哪怕是手工塞进来的）一律视作未激活。
    // 本地文件按产品分只是第一道，这里再按内容核一次。
    const auto p = parsed["product"].toString();
    if (p.isNotEmpty() && normalizeProductId (p) != normalizeProductId (product::id()))
        return {};

    return parsed;
}

void LicenseManager::saveLicense (const juce::var& license)
{
    if (license.getDynamicObject() == nullptr)
        return;

    const auto p = license["product"].toString();
    if (p.isNotEmpty() && normalizeProductId (p) != normalizeProductId (product::id()))
        return;   // 服务器返回的型号与本机不符，不落盘（双保险）

    auto now = juce::Time::currentTimeMillis();

    juce::var stored (new juce::DynamicObject());
    stored.getDynamicObject()->setProperty ("license_key", license["license_key"]);
    stored.getDynamicObject()->setProperty ("code",        license["code"]);
    stored.getDynamicObject()->setProperty ("product",     license["product"]);
    stored.getDynamicObject()->setProperty ("plan",        license["plan"]);
    stored.getDynamicObject()->setProperty ("max_devices", license["max_devices"]);
    stored.getDynamicObject()->setProperty ("expires_at",  license["expires_at"]);
    stored.getDynamicObject()->setProperty ("features",    license["features"]);
    stored.getDynamicObject()->setProperty ("machine_id",  getMachineId());
    stored.getDynamicObject()->setProperty ("saved_at",    now);
    stored.getDynamicObject()->setProperty ("last_verify", now);

    auto dir = licenseDir();
    dir.createDirectory();
    licenseFile().replaceWithText (scramble (juce::JSON::toString (stored)));
}

void LicenseManager::touchLastVerify()
{
    auto lic = readLicense();
    if (lic.getDynamicObject() == nullptr)
        return;

    lic.getDynamicObject()->setProperty ("last_verify", juce::Time::currentTimeMillis());
    licenseFile().replaceWithText (scramble (juce::JSON::toString (lic)));
}

juce::var LicenseManager::errObj (const juce::String& code, const juce::String& msg)
{
    juce::var v (new juce::DynamicObject());
    v.getDynamicObject()->setProperty ("ok", false);
    v.getDynamicObject()->setProperty ("code", code);
    v.getDynamicObject()->setProperty ("error", msg);
    return v;
}

juce::var LicenseManager::postJson (const juce::String& action, const juce::var& body)
{
    const juce::String url = apiBase() + "?action=" + action;

    juce::URL u (url);
    u = u.withPOSTData (juce::JSON::toString (body));

    // withPOSTData 会自动让请求走 POST；Content-Type 必须显式给，否则服务器按表单解析拿不到 JSON。
    // 该调用会阻塞直到首字节（或出错），调用方必须在线程外跑（见 LicenseOverlay）。
    auto stream = u.createInputStream (
        juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
            .withExtraHeaders ("Content-Type: application/json")
            .withConnectionTimeoutMs (15000)
            .withNumRedirectsToFollow (5));

    if (stream == nullptr)
        return errObj ("NETWORK", "网络连接失败，请检查网络后重试");

    auto text = stream->readEntireStreamAsString();
    auto parsed = juce::JSON::parse (text);

    if (parsed.isVoid() || parsed.getDynamicObject() == nullptr)
        return errObj ("PARSE", "服务器返回异常，请稍后重试");

    // 服务器业务错误统一 HTTP 200 + body.code；非 200（如 404/410/409/403/401）也读 body
    return parsed;
}

juce::var LicenseManager::activate (const juce::String& code,
                                    const juce::String& deviceName,
                                    const juce::String& os,
                                    const juce::String& plugin,
                                    const juce::String& version)
{
    juce::var body (new juce::DynamicObject());
    body.getDynamicObject()->setProperty ("code",       code.trim());
    body.getDynamicObject()->setProperty ("machine_id", getMachineId());
    if (deviceName.isNotEmpty()) body.getDynamicObject()->setProperty ("device_name", deviceName);
    if (os.isNotEmpty())          body.getDynamicObject()->setProperty ("os",          os);
    if (plugin.isNotEmpty())      body.getDynamicObject()->setProperty ("plugin",     plugin);   // 留空 = 不校验产品
    if (version.isNotEmpty())     body.getDynamicObject()->setProperty ("version",    version);

    auto res = postJson ("activate", body);

    if (res["ok"])
    {
        auto lic = res["license"];
        if (lic.getDynamicObject() != nullptr)
            saveLicense (lic);
    }

    return res;
}

juce::var LicenseManager::verify()
{
    auto lic = readLicense();
    if (lic.getDynamicObject() == nullptr)
        return errObj ("NO_LICENSE", "本地无授权，请先激活");

    auto key = lic["license_key"].toString();
    auto mid = lic["machine_id"].toString();
    if (key.isEmpty() || mid.isEmpty())
        return errObj ("BAD_REQUEST", "本地授权数据缺失，请重新激活");

    juce::var body (new juce::DynamicObject());
    body.getDynamicObject()->setProperty ("license_key", key);
    body.getDynamicObject()->setProperty ("machine_id",  mid);
    body.getDynamicObject()->setProperty ("plugin",      product::id());

    auto res = postJson ("verify", body);

    bool revoked = ! ((bool) res["ok"]);
    auto code = res["code"].toString();
    if (revoked && (code == "UNBOUND" || code == "LICENSE_EXPIRED"
                    || code == "LICENSE_INVALID" || code == "PRODUCT_MISMATCH"))
    {
        logout(); // 吊销/过期：清掉本地授权，下次启动走锁定引导
    }
    else if ((bool) res["ok"])
    {
        touchLastVerify();
    }

    return res;
}

juce::var LicenseManager::getState()
{
    juce::var st (new juce::DynamicObject());
    st.getDynamicObject()->setProperty ("machineId",  getMachineId());
    st.getDynamicObject()->setProperty ("productId",  product::id());

    auto lic = readLicense();
    if (lic.getDynamicObject() == nullptr)
    {
        st.getDynamicObject()->setProperty ("activated", false);
        return st;
    }

    st.getDynamicObject()->setProperty ("activated",  true);
    st.getDynamicObject()->setProperty ("product",    lic["product"]);
    st.getDynamicObject()->setProperty ("plan",       lic["plan"]);
    st.getDynamicObject()->setProperty ("expiresAt",  lic["expires_at"]);
    st.getDynamicObject()->setProperty ("features",   lic["features"]);
    st.getDynamicObject()->setProperty ("maxDevices", lic["max_devices"]);
    st.getDynamicObject()->setProperty ("code",       lic["code"]);
    st.getDynamicObject()->setProperty ("savedAt",    lic["saved_at"]);
    st.getDynamicObject()->setProperty ("lastVerify", lic["last_verify"]);
    return st;
}

void LicenseManager::logout()
{
    licenseFile().deleteFile();
}

bool LicenseManager::shouldVerifyOnLaunch()
{
    if (! hasLicense())
        return false;

    auto lic = readLicense();
    if (lic.getDynamicObject() == nullptr)
        return false;

    const auto exp = lic["expires_at"].toString();
    if (! exp.isEmpty())
        return true; // 有过期时间的授权：每次启动都问一下，别放过过期

    const auto last = (juce::int64) lic["last_verify"];
    const auto now = juce::Time::currentTimeMillis();
    if (last <= 0)
        return true;

    return (now - last) >= kVerifyIntervalMs;
}

} // namespace taiji
