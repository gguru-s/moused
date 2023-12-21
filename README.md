# What?

Forked from rvaiya/moused for personal config and added application mapper code from rvaiya/keyd repo.

# Install & Run

Add the user to moused group

	make install 
Run

	/<path_to_application-mapper.py

 This will start the moused app and update the mappings based on active window.

# Config

Global mappings are configured in /etc/moused/moused.conf. The file has the following format

	[<mouse name>]
	
	
	<LHS> = <Action>

## Example	
	[Logitech M570]

	scroll_swap_axes=1
	scrollmode_sensitivity=1.5
	scroll_inhibit_x = 1
	btn9 = btn1t
	btn8 = sensitivity(.25)
	scrolldown = scrollon
	scrollup = scrolloff
 
 Per-app configurations are in $HOME/.config/moused/app.conf. The file has the following format

	[<window name>]
	
	
	<LHS> = <Action>

## Example	
	[firefox]

	scrolldown = btn1
	scrollup = btn1

# Options

## LHS

 - btn[0-9]
 - scroll(up|down|left|right)
 - btn[0-9]t - toggle variant of buttons 0-9

## Actions

 - scrollon
 - scrolloff
 - scrollt 
 - sensitivity(<num>)
 - btn[0-9]

E.G

	scrollup=scrolloff
	scrolldown=scrollon

Allows you to activate scroll mode by moving the scroll wheel one or more
notches down and deactivate it by scrolling up. This is particularly useful for
trackballs with a limited number of buttons.
