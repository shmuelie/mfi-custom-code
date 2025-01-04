# cfgmtd

```shell
Usage: cfgmtd [options]
        -t <type>                       - Configuration type to use [1(active)|2(backup)]. (Default: 1(active))
        -f <config file>                - Configuration file to use. (Default: /tmp/system.cfg)
        -p <persistent directory>       - Directory to persistent dir. (Default: none)
        -w                              - Write to flash action.
        -r                              - Read from flash action.
        -o <mtd|file name>              - Use mtd or file name. (Default: cfg)
        -h                              - This message.
```

## Investigation

`/bin/tar -cz -O -C %s persistent` is used to create a tarball of the persistent directory. The ouput is then saved to the MTD.

`/bin/tar -xz -f - -C %s` is used to extract the tarball from the MTD and extract it to the persistent directory.