Unicode True

!include "MUI2.nsh"
!include "WinVer.nsh"
!include "x64.nsh"

!define PRODUCT "ImageWalker Collection"
!define PUBLISHER "Zac Walker"
!define UNINSTALL_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\ImageWalkerCollection"
!define SHORTCUT_DIR "$SMPROGRAMS\${PRODUCT}"

Var PreflightPath

Name "${PRODUCT}"
OutFile "${RELEASE_DIR}\imagewalker-setup.exe"
InstallDir "$LOCALAPPDATA\Programs\${PRODUCT}"
InstallDirRegKey HKCU "${UNINSTALL_KEY}" "InstallLocation"
RequestExecutionLevel user
SetCompressor /SOLID lzma

VIProductVersion "${COLLECTION_VERSION}"
VIAddVersionKey "ProductName" "${PRODUCT}"
VIAddVersionKey "ProductVersion" "${COLLECTION_VERSION}"
VIAddVersionKey "FileDescription" "ImageWalker Collection Setup"
VIAddVersionKey "FileVersion" "${COLLECTION_VERSION}"
VIAddVersionKey "CompanyName" "${PUBLISHER}"
VIAddVersionKey "LegalCopyright" "Copyright (c) Zac Walker"

!define MUI_ICON "setup.ico"
!define MUI_UNICON "setup.ico"
!define MUI_ABORTWARNING
!define MUI_WELCOMEPAGE_TEXT "Install ImageWalker 1.0, 2.0, 2.2, 2.3 and 3.0 side by side.$\r$\n$\r$\nSetup picks the right 32-bit or 64-bit build for this PC. Choose an edition from the ImageWalker Collection folder in Start."
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"

!ifdef SIGN_TOOL
    !uninstfinalize '"${SIGN_TOOL}" sign /fd SHA256 /sha1 ${CERTIFICATE} /d "ImageWalker Collection Uninstaller" /tr "${TIMESTAMP_URL}" /td SHA256 "%1"' = 0
!endif

!macro ImageWalkerVersions Action
    !insertmacro ${Action} 10 "1.0"
    !insertmacro ${Action} 20 "2.0"
    !insertmacro ${Action} 22 "2.2"
    !insertmacro ${Action} 23 "2.3"
    !insertmacro ${Action} 30 "3.0"
!macroend

!macro FileX64 Id Label
    File "${PAYLOAD_DIR}\x64\imagewalker${Id}.exe"
    IfErrors install_failed
!macroend

!macro FileX86 Id Label
    File "${PAYLOAD_DIR}\x86\imagewalker${Id}.exe"
    IfErrors install_failed
!macroend

!macro MakeShortcut Id Label
    CreateShortcut "${SHORTCUT_DIR}\ImageWalker ${Label}.lnk" "$INSTDIR\imagewalker${Id}.exe"
    IfErrors install_failed
!macroend

; Read the package script's generated Delete lines as data. Use the same
; exact names for extraction, preflight and removal, not a wildcard on disk.
!define /file LicenseManifest "${PAYLOAD_DIR}\ImageWalkerLicenses.nsh"
!macro LicenseFiles Action
    !searchparse `${LicenseRemaining}` `Delete "$INSTDIR\Licenses\` LicenseName `"` LicenseTail
    !insertmacro ${Action} "Licenses\${LicenseName}"
    !undef LicenseName
    !define /redef LicenseRemaining `${LicenseTail}`
    !undef LicenseTail
    !if `${LicenseRemaining}` != ""
        !insertmacro LicenseFiles ${Action}
    !endif
!macroend

!macro SharedFiles Action
    !insertmacro ${Action} "Readme.txt"
    !insertmacro ${Action} "dic\en_GB.aff"
    !insertmacro ${Action} "dic\en_GB.dic"
    !insertmacro ${Action} "dic\en_US.aff"
    !insertmacro ${Action} "dic\en_US.dic"
    !define LicenseRemaining `${LicenseManifest}`
    !insertmacro LicenseFiles ${Action}
    !undef LicenseRemaining
!macroend

!macro InstallShared RelativePath
    File "/oname=$INSTDIR\${RelativePath}" "${PAYLOAD_DIR}\shared\${RelativePath}"
    IfErrors install_failed
!macroend

!macro RemoveManaged Path
    Delete "${Path}"
    IfErrors uninstall_failed
!macroend

!macro RemoveEdition Id Label
    !insertmacro RemoveManaged "$INSTDIR\imagewalker${Id}.exe"
    !insertmacro RemoveManaged "${SHORTCUT_DIR}\ImageWalker ${Label}.lnk"
!macroend

!macro RemoveShared RelativePath
    !insertmacro RemoveManaged "$INSTDIR\${RelativePath}"
!macroend

!macro CheckManagedPath Path ExpectedDirectoryBit
    StrCpy $PreflightPath "${Path}"
    StrCpy $0 ${ExpectedDirectoryBit}
    !ifdef __UNINSTALL__
        Call un.CheckManagedPath
    !else
        Call CheckManagedPath
    !endif
    IfErrors preflight_failed
!macroend

!macro CheckEdition Id Label
    !insertmacro CheckManagedPath "$INSTDIR\imagewalker${Id}.exe" 0
    !insertmacro CheckManagedPath "${SHORTCUT_DIR}\ImageWalker ${Label}.lnk" 0
!macroend

!macro CheckShared RelativePath
    !insertmacro CheckManagedPath "$INSTDIR\${RelativePath}" 0
!macroend

!macro Preflight
    !insertmacro CheckManagedPath "$INSTDIR" 0x10
    !insertmacro CheckManagedPath "$INSTDIR\dic" 0x10
    !insertmacro CheckManagedPath "$INSTDIR\Licenses" 0x10
    !insertmacro CheckManagedPath "${SHORTCUT_DIR}" 0x10
    !insertmacro ImageWalkerVersions CheckEdition
    !insertmacro SharedFiles CheckShared
    !insertmacro CheckManagedPath "$INSTDIR\Uninstall.exe" 0
!macroend

!macro DefinePathCheck Prefix FileAccess CheckDirectoryWrite
Function ${Prefix}CheckManagedPath
    ClearErrors
    System::Call 'kernel32::GetFileAttributesW(w "$PreflightPath") i.r1 ?e'
    Pop $2
    ${If} $1 = -1
        ; Only a missing file or path is safe to skip, not access denied.
        ${If} $2 <> 2
        ${AndIf} $2 <> 3
            SetErrors
        ${EndIf}
        Return
    ${EndIf}

    ; Reject unexpected directories/files and reparse points at managed paths.
    IntOp $2 $1 & 0x410
    ${If} $2 <> $0
        SetErrors
        Return
    ${EndIf}
    ${If} $0 = 0
        IntOp $2 $1 & 1 ; FILE_ATTRIBUTE_READONLY
        ${If} $2 <> 0
            SetErrors
            Return
        ${EndIf}
        ; Exclusive OPEN_EXISTING: check write/delete sharing and ACLs without
        ; truncating or deleting anything. Never follow a final-component link.
        System::Call 'kernel32::CreateFileW(w "$PreflightPath", i ${FileAccess}, i 0, p 0, i 3, i 0x00200000, p 0) p.r1'
    ${Else}
        !if ${CheckDirectoryWrite} = 1
            ; FILE_ADD_FILE with BACKUP_SEMANTICS for an existing directory.
            System::Call 'kernel32::CreateFileW(w "$PreflightPath", i 2, i 7, p 0, i 3, i 0x02200000, p 0) p.r1'
        !else
            Return ; Uninstall only attempts non-recursive directory cleanup.
        !endif
    ${EndIf}

    ${If} $1 = -1
        SetErrors
    ${Else}
        System::Call 'kernel32::CloseHandle(p r1)'
    ${EndIf}
FunctionEnd
!macroend

; Both check GENERIC_WRITE to detect running EXEs; uninstall also needs DELETE.
!insertmacro DefinePathCheck "" 0x40000000 1
!insertmacro DefinePathCheck "un." 0x40010000 0

Function .onInit
    ${IfNot} ${AtLeastWin10}
        MessageBox MB_OK|MB_ICONSTOP "ImageWalker Collection requires Windows 10 or later." /SD IDOK
        SetErrorLevel 1
        Abort
    ${EndIf}
    ${IfNot} ${IsNativeAMD64}
    ${AndIfNot} ${IsNativeIA32}
        MessageBox MB_OK|MB_ICONSTOP "This installer supports x86 and x64 PCs." /SD IDOK
        SetErrorLevel 1
        Abort
    ${EndIf}
FunctionEnd

Section
    !insertmacro Preflight

    ClearErrors
    SetOutPath "$INSTDIR"
    IfErrors install_failed
    CreateDirectory "$INSTDIR\dic"
    IfErrors install_failed
    CreateDirectory "$INSTDIR\Licenses"
    IfErrors install_failed
    CreateDirectory "${SHORTCUT_DIR}"
    IfErrors install_failed
    ${If} ${IsNativeAMD64}
        !insertmacro ImageWalkerVersions FileX64
    ${Else}
        !insertmacro ImageWalkerVersions FileX86
    ${EndIf}

    SetOverwrite off
    File "${PAYLOAD_DIR}\shared\imagewalker30-tools.json"
    IfErrors install_failed
    SetOverwrite on
    !insertmacro SharedFiles InstallShared

    WriteUninstaller "$INSTDIR\Uninstall.exe"
    IfErrors install_failed
    !insertmacro ImageWalkerVersions MakeShortcut

    WriteRegStr HKCU "${UNINSTALL_KEY}" "DisplayName" "${PRODUCT}"
    WriteRegStr HKCU "${UNINSTALL_KEY}" "DisplayVersion" "${COLLECTION_VERSION}"
    WriteRegStr HKCU "${UNINSTALL_KEY}" "DisplayIcon" "$INSTDIR\imagewalker30.exe"
    WriteRegStr HKCU "${UNINSTALL_KEY}" "Publisher" "${PUBLISHER}"
    WriteRegStr HKCU "${UNINSTALL_KEY}" "InstallLocation" "$INSTDIR"
    WriteRegStr HKCU "${UNINSTALL_KEY}" "UninstallString" '"$INSTDIR\Uninstall.exe"'
    WriteRegStr HKCU "${UNINSTALL_KEY}" "QuietUninstallString" '"$INSTDIR\Uninstall.exe" /S'
    WriteRegStr HKCU "${UNINSTALL_KEY}" "URLInfoAbout" "https://imagewalker.com/"
    WriteRegStr HKCU "${UNINSTALL_KEY}" "HelpLink" "https://github.com/ZacWalk/imagewalker/issues"
    WriteRegDWORD HKCU "${UNINSTALL_KEY}" "NoModify" 1
    WriteRegDWORD HKCU "${UNINSTALL_KEY}" "NoRepair" 1
    IfErrors install_failed
    Goto installed

preflight_failed:
    DetailPrint "Update blocked: $PreflightPath"
    MessageBox MB_OK|MB_ICONSTOP "Setup made no changes. It cannot safely replace or write to:$\r$\n$\r$\n$PreflightPath$\r$\n$\r$\nClose all ImageWalker editions and any program using this path. Check that it is writable and is not a link, then retry." /SD IDOK
    SetErrorLevel 1
    Abort
install_failed:
    MessageBox MB_OK|MB_ICONSTOP "Setup did not finish. Close all ImageWalker windows and retry from a writable folder." /SD IDOK
    SetErrorLevel 1
    Abort
installed:
SectionEnd

Section "Uninstall"
    !insertmacro Preflight

    !insertmacro ImageWalkerVersions RemoveEdition
    !insertmacro SharedFiles RemoveShared
    !insertmacro RemoveManaged "$INSTDIR\Uninstall.exe"
    RMDir "${SHORTCUT_DIR}"
    RMDir "$INSTDIR\dic"
    RMDir "$INSTDIR\Licenses"
    RMDir "$INSTDIR"
    DeleteRegKey HKCU "${UNINSTALL_KEY}"
    Goto uninstalled

preflight_failed:
    DetailPrint "Removal blocked: $PreflightPath"
    MessageBox MB_OK|MB_ICONSTOP "Uninstall made no changes. It cannot safely remove:$\r$\n$\r$\n$PreflightPath$\r$\n$\r$\nClose all ImageWalker editions and any program using this path. Check that it is writable and is not a link, then retry." /SD IDOK
    SetErrorLevel 1
    Abort
uninstall_failed:
    MessageBox MB_OK|MB_ICONSTOP "Uninstall did not finish. Close all ImageWalker editions, check that the install folder is writable, then retry." /SD IDOK
    SetErrorLevel 1
    Abort
uninstalled:
SectionEnd
