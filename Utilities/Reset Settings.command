#!/bin/bash

deleteSettings()
{
	if [[ "$OSTYPE" == "darwin"* ]]; then
		# macOS
		rm -f ~/Library/Preferences/Light\ Host.settings
		echo "Settings reset for macOS."
	elif [[ "$OSTYPE" == "linux-gnu"* ]]; then
		# Linux
		rm -f ~/.config/Light\ Host.settings
		echo "Settings reset for Linux."
	else
		# Windows or other OS - show instructions
		echo "For Windows, delete the settings file at:"
		echo "%APPDATA%\\Light Host\\Light Host.settings"
	fi
}

echo "Reset settings for Light Host?"
select yn in "Yes" "No"; do
	case $yn in
		Yes ) deleteSettings; break;;
		No ) echo "Settings not altered."; break;;
	esac
done