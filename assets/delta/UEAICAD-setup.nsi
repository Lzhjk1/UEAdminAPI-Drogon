; UE AI CAD 增量更新安装器模板（仅 DELTA_MODE；服务端 DeltaBuilder 交叉编译用）
; 编译参数（由 DeltaBuilder 传入）：
;   -DDELTA_MODE -DDELTA_FROM_VERSION -DDELTA_INCLUDE=<delta_files.nsh>
;   -DSETUP_DIR=<输出目录> -DPRODUCT_MAJOR/MINOR/PATCH_VERSION
; 约定：
;   - 客户端以 /S /D=<当前安装目录> 调起；手动双击会被拒绝。
;   - 失败时写 %LOCALAPPDATA%\UE AI CAD\update\fallback.json，客户端据此回落全量。
;   - 卸载项由 electron-builder 创建：HKLM\...\Uninstall\UE AI CAD（DisplayVersion=产品版本）。

!include "LogicLib.nsh"
!include "FileFunc.nsh"

!ifndef DELTA_MODE
  !error "UEAICAD-setup.nsi 仅用于 -DDELTA_MODE"
!endif
!ifndef DELTA_FROM_VERSION
  !error "DELTA_MODE 需要 -DDELTA_FROM_VERSION=<基线版本>"
!endif
!ifndef DELTA_INCLUDE
  !error "DELTA_MODE 需要 -DDELTA_INCLUDE=<delta_files.nsh 路径>"
!endif
!ifndef SETUP_DIR
  !define SETUP_DIR "D:\UEAICAD安装包"
!endif
!ifndef PRODUCT_MAJOR_VERSION
  !define PRODUCT_MAJOR_VERSION 0
!endif
!ifndef PRODUCT_MINOR_VERSION
  !define PRODUCT_MINOR_VERSION 0
!endif
!ifndef PRODUCT_PATCH_VERSION
  !define PRODUCT_PATCH_VERSION 0
!endif
!define PRODUCT_VERSION "${PRODUCT_MAJOR_VERSION}.${PRODUCT_MINOR_VERSION}.${PRODUCT_PATCH_VERSION}"

!define UEAICAD_UNINST_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\UE AI CAD"

Name "UE AI CAD ${PRODUCT_VERSION} 增量更新"
OutFile "${SETUP_DIR}\UEAICAD_${PRODUCT_VERSION}_Delta_from_${DELTA_FROM_VERSION}.exe"
RequestExecutionLevel admin
InstallDir "$PROGRAMFILES\UEAICAD"
ShowInstDetails show
ShowUnInstDetails show
SetCompressor lzma

Function .onInit
  IfSilent ueaicad_delta_silent_ok
    MessageBox MB_ICONSTOP "增量更新包不能手动安装，请在 UE AI CAD 中检查更新。"
    Quit
  ueaicad_delta_silent_ok:

  ; 客户端通过 /S /D=<目录> 指定安装目录；未指定时读卸载项的安装位置
  ${If} $INSTDIR == "$PROGRAMFILES\UEAICAD"
    ReadRegStr $0 HKLM "${UEAICAD_UNINST_KEY}" "InstallLocation"
    ${If} $0 != ""
      StrCpy $INSTDIR $0
    ${EndIf}
  ${EndIf}
  ${IfNot} ${FileExists} "$INSTDIR\UE AI CAD.exe"
    StrCpy $R9 "install-dir-missing"
    Call DeltaWriteFallback
    DetailPrint "未找到 UE AI CAD 安装目录（$INSTDIR），已中止"
    SetErrorLevel 2
    Quit
  ${EndIf}

  ; 基线必须精确等于生成差量时的版本
  ReadRegStr $0 HKLM "${UEAICAD_UNINST_KEY}" "DisplayVersion"
  StrCmp $0 "${DELTA_FROM_VERSION}" ueaicad_baseline_ok
    StrCpy $R9 "baseline-mismatch"
    Call DeltaWriteFallback
    DetailPrint "增量包基线不匹配（当前 $0，需要 ${DELTA_FROM_VERSION}），已中止"
    SetErrorLevel 2
    Quit
  ueaicad_baseline_ok:
FunctionEnd

; $R9 = 失败原因；写回落标记供客户端强制切全量
Function DeltaWriteFallback
  CreateDirectory "$LOCALAPPDATA\UE AI CAD\update"
  FileOpen $9 "$LOCALAPPDATA\UE AI CAD\update\fallback.json" w
  FileWrite $9 '{"fromVersion":"${DELTA_FROM_VERSION}","toVersion":"${PRODUCT_VERSION}","reason":"'
  FileWrite $9 $R9
  FileWrite $9 '"}'
  FileClose $9
FunctionEnd

Section "Delta"
  ; delta_files.nsh 由 DeltaBuilder 按 manifest 生成：
  ; 备份被替换/删除的文件 -> 覆盖 -> certutil 逐文件校验 -> 删除过期文件；
  ; 任一校验失败即写 fallback 并以退出码 3 中止。
  !include "${DELTA_INCLUDE}"
  DetailPrint "UE AI CAD 增量更新完成：${DELTA_FROM_VERSION} -> ${PRODUCT_VERSION}"
SectionEnd
