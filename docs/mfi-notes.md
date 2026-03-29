# Useful mFi Notes

- `/var/etc/persistent/` is the home directory when you SSH in and the
  "persistent" folder.
- The `save` alias will save the persistent folder's data to flash memory, so it
  will be there after a reboot. The flash memory is only 128KB, split in half for
  backup — 64KB of usable space.
- If `/var/etc/persistent/profile` exists it will be run on login.
- If `/var/etc/persistent/rc.poststart` exists it will be run after the device
  starts up. NOTE: the firmware waits 3 minutes after startup before running
  your script.
