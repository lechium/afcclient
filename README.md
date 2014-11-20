afcclient
=========

A simple CLI interface to AFC via libimobiledevice.


## Requirements

- libimobiledevice (v 1.1.5+) (windows and mac libs included)
  https://github.com/libimobiledevice/libimobiledevice

- if building for windows you will need mingw (http://mingw.org/wiki/Getting_Started)

## Building

    $ make

## Running

    if you are using the included libraries for mac / windows you will need to copy them into the same location
    as afcclient binary

## Usage

    Usage: afcclient [ra:u:vh] command cmdargs...

      Options:
        -r, --root                 Use the afc2 server if jailbroken (ignored with -a)
        -a, --appid=<APP-ID>       Access bundle directory for app-id
        -u, --uuid=<UDID>          Specify the device udid
        -v, --verbose              Enable verbose debug messages
        -h, --help                 Display this help message
        -l, --list                 List devices
        -c, --clean                Cleans out folder after exporting/cloning

      New commands:

        clone  [localpath]         clone Documents folder into a local folder. (requires appid)
        export [path] [localpath]  export a specific directory to a local one (not recursive)
        documents                  recursive plist formatted list of entire ~/Documents folder (requires appid)

      Where "command" and "cmdargs..." are as folows:

        devinfo                    dump device info from AFC server
        ls <dir> [dir2...]         list remote directory contents
        info <path> [path2...]     dump remote file information
        mkdir <path> [path2...]    create directory at path
        rm <path> [path2...]       remove directory at path
        rename <from> <to>         rename path 'from' to path 'to'
        link <target> <link>       create a hard-link from 'link' to 'target'
        symlink <target> <link>    create a symbolic-link from 'link' to 'target'
        cat <path>                 cat contents of <path> to stdout
        get <path> [localpath]     download a file (default: current dir)
        put <localpath> [path]     upload a file (default: remote top-level dir)


## Known Issues / TODO

- listing output is fugly

## Author

Eric Monti - esmonti at gmail dot com

tweaked by Kevin Bradley to remove clang requirement, include built libs for mac and windows (32 bit only for windows)
and added some improved recursive listing functionality with plist output.

## License

MIT - See LICENSE.txt
