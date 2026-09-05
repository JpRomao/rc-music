; Compiled only for the isolated registry contract tests, never distributed.
Unicode true
RequestExecutionLevel user
SilentInstall silent
SilentUnInstall silent
SetCompressor zlib
Name "RC Music association contract test"
OutFile "${RCMUSIC_TEST_OUTPUT}"
InstallDir "${RCMUSIC_TEST_INSTALL_DIR}"

!ifndef RCMUSIC_TEST_PREFIX
    !error "An isolated test registry prefix is required"
!endif
!define RCMUSIC_REGISTRY_BASE "${RCMUSIC_TEST_PREFIX}\Software"
!macro RCMUSIC_FOR_EACH_FORMAT CALLBACK
    !insertmacro ${CALLBACK} "mp3" "MP3 audio"
    !insertmacro ${CALLBACK} "wav" "WAV audio"
!macroend
!addincludedir ".."
!include "RcMusicAssociationSections.nsh"

Section
    SetShellVarContext current
    SetRegView 64
    CreateDirectory "$INSTDIR"
    ; An inert marker, not an executable and never launched by these tests.
    FileOpen $0 "$INSTDIR\RCMusic.exe" w
    FileWrite $0 "synthetic association target"
    FileClose $0
    WriteUninstaller "$INSTDIR\Uninstall.exe"
    !insertmacro RCMUSIC_INSTALL_ASSOCIATIONS
SectionEnd

Section "Uninstall"
    SetShellVarContext current
    SetRegView 64
    !insertmacro RCMUSIC_UNINSTALL_ASSOCIATIONS
SectionEnd
