# librubbish

`librubbish.so` 的 C++/CMake 重写。目标是保持原始 Android JNI 库的 ABI 和可观察行为，而不是嵌入或复制原二进制。

## 已还原的接口

- `JNI_OnLoad(JavaVM*, void*)`：取得 `JNIEnv`，查找 `com/xtc/secretapi/XTCGetUtilWrapper`，动态注册 `gete()Ljava/lang/String;`。
- `gete()`：与原始实现一致，固定返回 `xtc_ok`。
- `Java_com_xtc_secretapi_XTCGetUtilWrapper_init(JNIEnv*, jobject, jstring)`：读取当前 APK 证书，计算证书 DER 的 SHA-1，转为大写十六进制字符串并与参数比较；相等返回 `xtc_ok`，否则返回 `xtc_error`。
- 同时保留原库导出的 `getApplication`、`get6`、`jstringToChar`、`getSignArray`、`AUTH_KEY` 和 `ERROR_KEY` 符号。

原 ELF 只依赖 Android 系统库 `liblog`、`libm`、`libstdc++`、`libdl`、`libc`，没有第三方库，因此本工程不需要 `FetchContent`。C++ 运行时使用 NDK 的静态 `c++_static`，避免新增单独的 `libc++_shared.so` 运行时依赖。

## 构建

Windows 上 NDK 安装在标准 `%LOCALAPPDATA%\Android\Sdk\ndk` 目录时：

```powershell
.\scripts\build.ps1
```

显式指定 NDK 或 ABI：

```powershell
.\scripts\build.ps1 -NdkRoot C:\path\to\ndk -Abi armeabi-v7a -Configuration Release
.\scripts\build.ps1 -Abi arm64-v8a
```

产物：

```text
build/android-armeabi-v7a/librubbish.so
```

## 验证

```powershell
.\scripts\verify.ps1
```

脚本检查 ELF 架构、SONAME、必需导出符号并打印 SHA-256。

连接 ARMv7 Android 设备后，还可以运行最小 JNI 集成测试：

```powershell
.\scripts\test-on-device.ps1
```

测试应用会验证 `gete()`、正确签名摘要和错误摘要三条路径。

## 目录

```text
CMakeLists.txt
src/rubbish.cpp
scripts/build.ps1
scripts/verify.ps1
scripts/test-on-device.ps1
tests/android/
```
