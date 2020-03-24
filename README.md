# xinput-bridge
This is a simple application that sends the local gamepad state to a game/application on a remote machine. It only sends the gamepad state,
and only works with xinput. It has no support for sending any other data.

On the local machine you'll need to run `xinput-bridge.exe`, this reads the gamepad state and sends it to the specified target machine.

On the remote machine you'll need to place the fake-xinput.dll into the same directory as the application you want to control.
You'll need to rename it to some variant of `input1_3.dll`, depending on which dll the application you're trying to control is expecting
to find. (e.g. `xinput1_3.dll`, `xinput1_4.dll`, `xinput9_1_0.dll` if you don't know, it's probably fine to just make all three.)

# Security
This program just uses a UDP socket to send data between the client+server, there's no encryption or security, the assumption is
you'll be using this inside of a VPN or on a local network. Don't use this unprotected across the Internet.

# Uninstallation
Just delete the dlls you've copied to your applications directory, don't forget about this as you won't be able to control
your application locally if you do. (You can run `xinput-bridge.exe` and use `localhost` as the target to control an application on the local machine if you don't want to remove those dlls for whatever reason.)
