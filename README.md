# xinput-bridge
This is a simple application that sends the local gamepad state to a game/application on a remote machine. It only sends the gamepad state,
and only works with xinput. It has no support for sending any other data.

On the local machine you'll need to run `xinput-bridge.exe`, this reads the gamepad state and sends it to the specified target machine.

On the remote machine you'll need to place the fake-xinput.dll into the same directory as the application you want to control.
You'll need to rename it to some variant of `input1_3.dll`, depending on which dll the application you're trying to control is expecting
to find. (e.g. `xinput1_3.dll`, `xinput1_4.dll`, `xinput9_1_0.dll` if you don't know, it's probably fine to just make all three)
