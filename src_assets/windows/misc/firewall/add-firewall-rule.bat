@echo off

rem Get sunshine root directory
for %%I in ("%~dp0\..") do set "ROOT_DIR=%%~fI"

set RULE_NAME=Umbra
set LEGACY_RULE_NAME=Apollo
set PROGRAM_BIN="%ROOT_DIR%\sunshine.exe"

rem Remove inherited names before adding the current visible rules.
netsh advfirewall firewall delete rule name=%LEGACY_RULE_NAME% >nul 2>&1

rem Add the rule
netsh advfirewall firewall add rule name=%RULE_NAME% dir=in action=allow protocol=tcp program=%PROGRAM_BIN% enable=yes
netsh advfirewall firewall add rule name=%RULE_NAME% dir=in action=allow protocol=udp program=%PROGRAM_BIN% enable=yes

rem Aurora Link needs its own exclusion: it is a SEPARATE executable, so the rule above
rem does not cover it. Without this the pad host binds UDP :48200 and reports that it is
rem listening, but Windows silently auto-creates an inbound BLOCK rule for it (a
rem non-elevated process gets no prompt, just a block) and every pad packet from the Pi
rem is dropped -- controller input simply never arrives, with a completely healthy
rem looking host log. Measured on SKYNET 2026-07-29.
rem UDP only: its one inbound listener is the pad port; the agent connection is OUTBOUND
rem to 127.0.0.1, which needs no exclusion.
set LINK_RULE_NAME="Umbra Eclipse Link"
set LEGACY_LINK_RULE_NAME="Apollo Aurora Link"
set LINK_PROGRAM_BIN="%ROOT_DIR%\aurora-link.exe"

netsh advfirewall firewall delete rule name=%LEGACY_LINK_RULE_NAME% >nul 2>&1
netsh advfirewall firewall add rule name=%LINK_RULE_NAME% dir=in action=allow protocol=udp program=%LINK_PROGRAM_BIN% enable=yes
