# NSIS Packaging
# see options at: https://cmake.org/cmake/help/latest/cpack_gen/nsis.html

set(CPACK_NSIS_INSTALLED_ICON_NAME "${PROJECT__DIR}\\\\${PROJECT_EXE}")

# Extra install commands
# Restores permissions on the install directory
# Migrates config files from the root into the new config folder
# Install service
SET(CPACK_NSIS_EXTRA_INSTALL_COMMANDS
        "${CPACK_NSIS_EXTRA_INSTALL_COMMANDS}
        IfSilent +2 0
        # ExecShell 'open' 'https://docs.lizardbyte.dev/projects/sunshine'
        nsExec::ExecToLog 'icacls \\\"$INSTDIR\\\" /reset'
        nsExec::ExecToLog '\\\"$INSTDIR\\\\scripts\\\\update-path.bat\\\" add'
        nsExec::ExecToLog '\\\"$INSTDIR\\\\drivers\\\\sudovda\\\\install.bat\\\"'
        Pop $0
        StrCmp $0 0 DisplayReady
        StrCmp $0 3010 DisplayReboot
        MessageBox MB_OK|MB_ICONSTOP 'Virtual display installation failed (code $0). Umbra setup cannot continue.' /SD IDOK
        Abort
        DisplayReboot:
        SetRebootFlag true
        DisplayReady:
        nsExec::ExecToLog '\\\"$INSTDIR\\\\scripts\\\\migrate-config.bat\\\"'
        nsExec::ExecToLog '\\\"$INSTDIR\\\\scripts\\\\add-firewall-rule.bat\\\"'
        IfFileExists \\\"$INSTDIR\\\\scripts\\\\install-gamepad.ps1\\\" 0 GamepadReady
        nsExec::ExecToLog \
          'powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File \\\"$INSTDIR\\\\scripts\\\\install-gamepad.ps1\\\"'
        Pop $0
        StrCmp $0 0 GamepadReady
        StrCmp $0 3010 GamepadReboot
        MessageBox MB_OK|MB_ICONSTOP 'Virtual gamepad installation failed (code $0). Umbra setup cannot continue.' /SD IDOK
        Abort
        GamepadReboot:
        SetRebootFlag true
        GamepadReady:
        nsExec::ExecToLog '\\\"$INSTDIR\\\\scripts\\\\install-service.bat\\\"'
        Pop $0
        StrCmp $0 0 HostServiceReady
        MessageBox MB_OK|MB_ICONSTOP 'Host service setup failed (code $0). Files have been retained; resolve the error and retry.' /SD IDOK
        Abort
        HostServiceReady:
        IfFileExists \\\"$INSTDIR\\\\scripts\\\\autostart-service.bat\\\" 0 HostAutostartReady
        nsExec::ExecToLog '\\\"$INSTDIR\\\\scripts\\\\autostart-service.bat\\\"'
        Pop $0
        StrCmp $0 0 HostAutostartReady
        MessageBox MB_OK|MB_ICONSTOP 'Host automatic startup could not be configured (code $0).' /SD IDOK
        Abort
        HostAutostartReady:
        NoController:
        ")

# Extra uninstall commands
# Uninstall service
set(CPACK_NSIS_EXTRA_UNINSTALL_COMMANDS
        "${CPACK_NSIS_EXTRA_UNINSTALL_COMMANDS}
        nsExec::ExecToLog '\\\"$INSTDIR\\\\scripts\\\\uninstall-service.bat\\\"'
        Pop $0
        StrCmp $0 0 HostServiceRemoved
        MessageBox MB_OK|MB_ICONSTOP 'Host service could not be stopped and removed (code $0). Files have been retained; resolve the error and retry.' /SD IDOK
        Abort
        HostServiceRemoved:
        nsExec::ExecToLog 'powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File \\\"$INSTDIR\\\\scripts\\\\stop-owned-processes.ps1\\\"'
        Pop $0
        StrCmp $0 0 HostProcessesStopped
        MessageBox MB_OK|MB_ICONSTOP 'Umbra processes could not be stopped safely (code $0). Files have been retained; resolve the error and retry.' /SD IDOK
        Abort
        HostProcessesStopped:
        nsExec::ExecToLog '\\\"$INSTDIR\\\\scripts\\\\delete-firewall-rule.bat\\\"'
        nsExec::ExecToLog '\\\"$INSTDIR\\\\sunshine.exe\\\" --restore-nvprefs-undo'
        IfFileExists \\\"$INSTDIR\\\\scripts\\\\uninstall-gamepad.ps1\\\" 0 NoGamepad
        MessageBox MB_YESNO|MB_ICONQUESTION \
            'Do you want to remove Virtual Gamepad?' \
            /SD IDNO IDNO NoGamepad
            nsExec::ExecToLog \
              'powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File \\\"$INSTDIR\\\\scripts\\\\uninstall-gamepad.ps1\\\"'
            Pop $0
            StrCmp $0 0 NoGamepad
            StrCmp $0 3010 GamepadRemovedReboot
            MessageBox MB_OK|MB_ICONSTOP 'Virtual gamepad removal failed (code $0). Files have been retained; resolve the error and retry.' /SD IDOK
            Abort
            GamepadRemovedReboot:
            SetRebootFlag true
        NoGamepad:
        MessageBox MB_YESNO|MB_ICONQUESTION \
            'Do you want to remove SudoVDA Virtual Display Driver?' \
            /SD IDNO IDNO NoSudoVDA
            nsExec::ExecToLog '\\\"$INSTDIR\\\\drivers\\\\sudovda\\\\uninstall.bat\\\"'; skipped if no
            Pop $0
            StrCmp $0 0 NoSudoVDA
            StrCmp $0 3010 DisplayRemovedReboot
            MessageBox MB_OK|MB_ICONSTOP 'Virtual display removal failed (code $0). Files have been retained; resolve the error and retry.' /SD IDOK
            Abort
            DisplayRemovedReboot:
            SetRebootFlag true
        NoSudoVDA:
        nsExec::ExecToLog '\\\"$INSTDIR\\\\scripts\\\\update-path.bat\\\" remove'
        MessageBox MB_YESNO|MB_ICONQUESTION \
            'Do you want to remove $INSTDIR (this includes the configuration, cover images, and settings)?' \
            /SD IDNO IDNO NoDelete
            RMDir /r \\\"$INSTDIR\\\"; skipped if no
            Delete \\\"$LOCALAPPDATA\\\\SudoMaker\\\\Apollo\\\\service_start_type.txt\\\"
            RMDir \\\"$LOCALAPPDATA\\\\SudoMaker\\\\Apollo\\\"
            RMDir \\\"$LOCALAPPDATA\\\\SudoMaker\\\"
        NoDelete:
        ")

# Adding an option for the start menu
set(CPACK_NSIS_MODIFY_PATH OFF)
set(CPACK_NSIS_EXECUTABLES_DIRECTORY ".")
# This will be shown on the installed apps Windows settings
set(CPACK_NSIS_INSTALLED_ICON_NAME "sunshine.exe")
set(CPACK_NSIS_CREATE_ICONS_EXTRA
        "${CPACK_NSIS_CREATE_ICONS_EXTRA}
        SetOutPath '\$INSTDIR'
        CreateShortCut '\$SMPROGRAMS\\\\$STARTMENU_FOLDER\\\\${CMAKE_PROJECT_NAME}.lnk' \
            '\$INSTDIR\\\\sunshine.exe' '--shortcut'
        ")
set(CPACK_NSIS_DELETE_ICONS_EXTRA
        "${CPACK_NSIS_DELETE_ICONS_EXTRA}
        Delete '\$SMPROGRAMS\\\\$MUI_TEMP\\\\${CMAKE_PROJECT_NAME}.lnk'
        ")

# Checking for previous installed versions
set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL "ON")

# set(CPACK_NSIS_HELP_LINK "https://docs.lizardbyte.dev/projects/sunshine/latest/md_docs_2getting__started.html")
# set(CPACK_NSIS_URL_INFO_ABOUT "${CMAKE_PROJECT_HOMEPAGE_URL}")
# set(CPACK_NSIS_CONTACT "${CMAKE_PROJECT_HOMEPAGE_URL}/support")

# set(CPACK_NSIS_MENU_LINKS
#         "https://docs.lizardbyte.dev/projects/sunshine" "Sunshine documentation"
#         "https://app.lizardbyte.dev" "LizardByte Web Site"
#         "https://app.lizardbyte.dev/support" "LizardByte Support")
set(CPACK_NSIS_MANIFEST_DPI_AWARE true)
