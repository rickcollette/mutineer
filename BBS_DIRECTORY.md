# BBS DIRECTORY UPDATER

In conf/mutinerr.conf, we have added some configuration parameters for the BBS directory updater. These parameters are:

```
..appended to # Minimal config (key=value)
hostname=localhost # This is the name that will be displayed in the BBS directory, and also used for the telnet banner. If you are running behind a NAT, this should be your public hostname or IP address.

# BBS Directory configuration (this connects to the BBS directory server, and posts your BBS information to it)
bbs_dir_enabled=1
bbs_dir_server=mutineerbbs.com
bbs_dir_show_sysop=1
bbs_dir_show_bbs_name=1
bbs_dir_show_connections=1
bbs_dir_show_hostname=1 # This can be an ip as well 
```

we need to add functionality to mutineer core for this - it should get called every 15 minutes, and post the data allowed if it is enabled.

the payload is JSON:

```
{
  "sysop": "Sysop Name",
  "bbs_name": "BBS Name",
  "connections": 5,
  "hostname": "bbs.example.com"
  "port": 2929
}
```

and should be posted to ${bbs_dir_server}/api/bbs_directory


