; ============================================================
; relayFile NSIS 打包脚本 (需要 NSIS 3.x, Unicode)
;
; 用法(四个参数均必填, 未传入时编译报错退出):
;   makensis.exe /DSRC_BIN_DIR="<exe所在目录>" /DOUT_DIR="<安装包输出目录>" /DPRODUCT_VERSION="<版本号>" /DCOMMIT_ID="<commit短哈希,8位>" relayFile.nsi
;
;   例(写在一行, 路径含空格时加引号):
;     makensis.exe /DSRC_BIN_DIR="D:\360Downloads\relayFile\build-release\bin\Release" /DOUT_DIR="D:\out" /DPRODUCT_VERSION="1.0.0" /DCOMMIT_ID="abc12345" relayFile.nsi
;
; 产物: OUT_DIR\relayFile-setup-<版本>-<commit>.exe
;
; 安装行为:
;   - 用户级安装, 不需要管理员权限(RequestExecutionLevel user)
;   - 默认安装到 当前用户目录\relayFile, 即 %USERPROFILE%\relayFile
;   - 桌面创建快捷方式: relayGui -> relayFileGui.exe, relayServer -> relayFileServer.exe
;   - 写入 HKCU 卸载信息, 可在"应用和功能"中卸载, 安装目录内提供 Uninstall.exe
; ============================================================

Unicode true
ManifestDPIAware true

; ---------------- 必填参数(命令行 /D 传入, 未传则报错退出) ----------------
!ifndef SRC_BIN_DIR
  !define SRC_BIN_DIR ""
!endif
!ifndef OUT_DIR
  !define OUT_DIR ""
!endif
!ifndef PRODUCT_VERSION
  !define PRODUCT_VERSION ""
!endif
!ifndef COMMIT_ID
  !define COMMIT_ID ""
!endif

!if "${SRC_BIN_DIR}" == ""
  !error "缺少 SRC_BIN_DIR(exe所在目录). 用法: makensis /DSRC_BIN_DIR=<exe目录> /DOUT_DIR=<输出目录> /DPRODUCT_VERSION=<版本号> /DCOMMIT_ID=<commit8位> relayFile.nsi"
!endif
!if "${OUT_DIR}" == ""
  !error "缺少 OUT_DIR(安装包输出目录). 用法: makensis /DSRC_BIN_DIR=<exe目录> /DOUT_DIR=<输出目录> /DPRODUCT_VERSION=<版本号> /DCOMMIT_ID=<commit8位> relayFile.nsi"
!endif
!if "${PRODUCT_VERSION}" == ""
  !error "缺少 PRODUCT_VERSION(版本号). 用法: makensis /DSRC_BIN_DIR=<exe目录> /DOUT_DIR=<输出目录> /DPRODUCT_VERSION=<版本号> /DCOMMIT_ID=<commit8位> relayFile.nsi"
!endif
!if "${COMMIT_ID}" == ""
  !error "缺少 COMMIT_ID(commit短哈希8位). 用法: makensis /DSRC_BIN_DIR=<exe目录> /DOUT_DIR=<输出目录> /DPRODUCT_VERSION=<版本号> /DCOMMIT_ID=<commit8位> relayFile.nsi"
!endif

!define PRODUCT_NAME "relayFile"
!define APP_KEY "Software\${PRODUCT_NAME}"
!define UNINST_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\${PRODUCT_NAME}"

; ---------------- 安装包基本设置 ----------------
Name "${PRODUCT_NAME} ${PRODUCT_VERSION}"
OutFile "${OUT_DIR}\${PRODUCT_NAME}-x64-v${PRODUCT_VERSION}-${COMMIT_ID}.exe"
InstallDir "$PROFILE\relayFile"          ; 默认: 当前用户目录\relayFile
InstallDirRegKey HKCU "${APP_KEY}" "InstallDir"  ; 二次安装时记住上次目录
RequestExecutionLevel user               ; 用户级安装, 不弹 UAC, $PROFILE/$DESKTOP 均为当前用户
SetCompressor /SOLID lzma

; ---------------- 页面 ----------------
!include "MUI2.nsh"

!define MUI_ABORTWARNING
!define MUI_FINISHPAGE_RUN "$INSTDIR\relayFileGui.exe"
!define MUI_FINISHPAGE_RUN_TEXT "启动 relayGui"

!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "English"
!insertmacro MUI_LANGUAGE "SimpChinese"

; ---------------- 辅助宏 ----------------
; 结束正在运行的程序, 防止文件被占用导致复制/删除失败
!macro KillApp exeName
  nsExec::ExecToLog 'taskkill /F /IM "${exeName}"'
  Pop $0
!macroend

; ---------------- 安装 ----------------
Section "Install"
  ; 程序正在运行则先结束
  !insertmacro KillApp "relayFileGui.exe"
  !insertmacro KillApp "relayFileServer.exe"
  !insertmacro KillApp "relayFileClient.exe"

  SetOutPath "$INSTDIR"
  ; 复制源目录下所有文件(含子目录)
  File /r "${SRC_BIN_DIR}\*.*"

  ; 桌面快捷方式
  CreateShortCut "$DESKTOP\relayGui.lnk" "$INSTDIR\relayFileGui.exe"
  CreateShortCut "$DESKTOP\relayServer.lnk" "$INSTDIR\relayFileServer.exe"

  ; 卸载程序 + 记住安装目录
  WriteUninstaller "$INSTDIR\Uninstall.exe"
  WriteRegStr HKCU "${APP_KEY}" "InstallDir" "$INSTDIR"

  ; "应用和功能"卸载入口
  WriteRegStr HKCU "${UNINST_KEY}" "DisplayName" "${PRODUCT_NAME} ${PRODUCT_VERSION}"
  WriteRegStr HKCU "${UNINST_KEY}" "DisplayVersion" "${PRODUCT_VERSION}"
  WriteRegStr HKCU "${UNINST_KEY}" "Publisher" "${PRODUCT_NAME}"
  WriteRegStr HKCU "${UNINST_KEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKCU "${UNINST_KEY}" "DisplayIcon" "$INSTDIR\relayFileGui.exe"
  WriteRegStr HKCU "${UNINST_KEY}" "UninstallString" "$\"$INSTDIR\Uninstall.exe$\""
  WriteRegStr HKCU "${UNINST_KEY}" "QuietUninstallString" "$\"$INSTDIR\Uninstall.exe$\" /S"
  WriteRegDWORD HKCU "${UNINST_KEY}" "NoModify" 1
  WriteRegDWORD HKCU "${UNINST_KEY}" "NoRepair" 1
SectionEnd

; ---------------- 卸载 ----------------
Section "Uninstall"
  !insertmacro KillApp "relayFileGui.exe"
  !insertmacro KillApp "relayFileServer.exe"
  !insertmacro KillApp "relayFileClient.exe"

  Delete "$DESKTOP\relayGui.lnk"
  Delete "$DESKTOP\relayServer.lnk"

  ; 安装目录为独立目录, 整体清空
  RMDir /r "$INSTDIR"

  DeleteRegKey HKCU "${UNINST_KEY}"
  DeleteRegKey HKCU "${APP_KEY}"
SectionEnd
