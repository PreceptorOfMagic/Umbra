@echo off

set RULE_NAME=Apollo
set LINK_RULE_NAME="Apollo Aurora Link"

rem Delete the rule
netsh advfirewall firewall delete rule name=%RULE_NAME%

rem Aurora Link's exclusion is a separate rule name, so it needs its own delete or an
rem uninstall would leave it behind pointing at a path that no longer exists.
netsh advfirewall firewall delete rule name=%LINK_RULE_NAME%
