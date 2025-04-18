#!/bin/bash
# Reset settings script for NovaHost
# Created April 18, 2025

deleteSettings()
{
	if [[ "$OSTYPE" == "darwin"* ]]; then
		# macOS
		rm -f ~/Library/Preferences/Nova\ Host.settings
		echo "Settings reset for macOS."
	elif [[ "$OSTYPE" == "linux-gnu"* ]]; then
		# Linux
		rm -f ~/.config/Nova\ Host.settings
		echo "Settings reset for Linux."
	else
		# Windows or other OS - show instructions
		echo "For Windows, delete the settings file at:"
		echo "%APPDATA%\\NovaHost Developers\\Nova Host\\Nova Host.settings"
	fi
}

echo "Reset settings for Nova Host?"
select yn in "Yes" "No"; do
	case $yn in
		Yes ) deleteSettings; break;;
		No ) echo "Settings not altered."; break;;
	esac
done