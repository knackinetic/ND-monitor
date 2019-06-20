# Add more charts to Netdata

Netdata collects system metrics by itself. It has many [internal plugins](../collectors) for collecting most of the metrics presented by default when it starts, collecting data from `/proc`, `/sys` and other Linux kernel sources.

To collect non-system metrics, Netdata supports a plugin architecture. The following are the currently available external plugins:

- **[Web Servers](#web-servers)**, such as apache, nginx, nginx_plus, tomcat, litespeed
- **[Web Logs](#web-log-parsers)**, such as apache, nginx, lighttpd, gunicorn, squid access logs, apache cache.log
- **[Load Balancers](#load-balancers)**, like haproxy
- **[Message Brokers](#message-brokers)**, like rabbitmq, beanstalkd
- **[Database Servers](#database-servers)**, such as mysql, mariadb, postgres, couchdb, mongodb, rethinkdb
- **[Social Sharing Servers](#social-sharing-servers)**, like retroshare
- **[Proxy Servers](#proxy-servers)**, like squid
- **[HTTP accelerators](#http-accelerators)**, like varnish cache
- **[Search engines](#search-engines)**, like elasticsearch
- **[Name Servers](#name-servers)** (DNS), like bind, nsd, powerdns, dnsdist
- **[DHCP Servers](#dhcp-servers)**, like ISC DHCP
- **[UPS](#ups)**, such as APC UPS, NUT
- **[RAID](#raid)**, such as MegaRAID
- **[Mail Servers](#mail-servers)**, like postfix, exim, dovecot
- **[File Servers](#file-servers)**, like samba, NFS, ftp, sftp, WebDAV
- **[Print Servers](#print-servers)**, like CUPS
- **[Hypervisors](#hypervisors)**, like XenServer, XCP-ng
- **[System](#system)**, for processes and other system metrics
- **[Sensors](#sensors)**, like temperature, fans speed, voltage, humidity, HDD/SSD S.M.A.R.T attributes
- **[Network](#network)**, such as SNMP devices, `fping`, access points, dns_query_time, nfacct
- **[Time Servers](#time-servers)**, like chrony
- **[Security](#security)**, like FreeRADIUS, OpenVPN, Fail2ban
- **[Telephony Servers](#telephony-servers)**, like openSIPS
- **[Go applications](#go-applications)**
- **[Household appliances](#household-appliances)**, like SMA WebBox (solar power), Fronius Symo solar power, Stiebel Eltron heating
- **[Java Processes](#java-processes)**, via JMX or Spring Boot Actuator
- **[Provisioning Systems](#provisioning-systems)**, like Puppet
- **[Game Servers](#game-servers)**, like SpigotMC
- **[Distributed Computing Clients](#distributed-computing-clients)**, like BOINC
- **[Skeleton Plugins](#skeleton-plugins)**, for writing your own data collectors

Check also [Third Party Plugins](Third-Party-Plugins.md) for a list of plugins distributed by third parties.

## configuring plugins

Netdata comes with **internal** and **external** plugins:

1. The **internal** ones are written in `C` and run as threads within the Netdata daemon.
2. The **external** ones can be written in any computer language. The Netdata daemon spawns these as processes (shown with `ps fax`) and reads their metrics using pipes (so the `stdout` of external plugins is connected to Netdata for metrics collection and the `stderr` of external plugins is connected to `/var/log/netdata/error.log`).

To make it easier to develop plugins, and minimize the number of threads and processes running, Netdata supports **plugin orchestrators**, each of them supporting one or more data collection **modules**. Currently we ship plugin orchestrators for 4 languages: `C`, `python`, `node.js` and `bash` and 2 more are under development (`go` and `java`).

#### enabling and disabling plugins

To control which plugins Netdata run, edit `netdata.conf` and check the `[plugins]` section. It looks like this:

```
[plugins]
	# enable running new plugins = yes
	# check for new plugins every = 60
	# proc = yes
	# diskspace = yes
	# cgroups = yes
	# cups = yes
	# tc = yes
	# nfacct = yes
	# idlejitter = yes
	# freeipmi = yes
	# node.d = yes
	# python.d = yes
	# fping = yes
	# ioping = yes
	# charts.d = yes
	# apps = yes
	# xenstat = yes
	# perf = no
```

The default for all plugins is the option `enable running new plugins`. So, setting this to `no` will disable all the plugins, except the ones specifically enabled.

#### enabling and disabling modules

Each of the **plugins** may support one or more data collection **modules**.  To control which of its modules run, you have to consult the configuration of the **plugin** (see table below).

#### modules configuration

Most **modules** come with **auto-detection**, configured to work out-of-the-box on popular operating systems with the default settings.

However, there are cases that auto-detection fails. Usually the reason is that the applications to be monitored do not allow Netdata to connect. In most of the cases, allowing the user `netdata` from `localhost` to connect and collect metrics, will automatically enable data collection for the application in question (it will require a Netdata restart).

You can verify Netdata **external plugins and their modules** are able to collect metrics, following this procedure:

```sh
# become user netdata
sudo su -s /bin/bash netdata

# execute the plugin in debug mode, for a specific module.
# example for the python plugin, mysql module:
/usr/libexec/netdata/plugins.d/python.d.plugin 1 debug trace mysql
```

Similarly, you can use `charts.d.plugin` for BASH plugins and `node.d.plugin` for node.js plugins.
Other plugins (like `apps.plugin`, `freeipmi.plugin`, `fping.plugin`, `ioping.plugin`, `nfacct.plugin`, `xenstat.plugin`, `perf.plugin`) use the native Netdata plugin API and can be run directly.

If you need to configure a Netdata plugin or module, all user supplied configuration is kept at `/etc/netdata` while the stock versions of all files is at `/usr/lib/netdata/conf.d`.
To copy a stock file and edit it, run `/etc/netdata/edit-config`. Running this command without an argument, will list the available stock files.

Each file should provide plenty of examples and documentation about each module and plugin.

This is a map of the all supported configuration options:

#### map of configuration files

plugin | language | plugin<br/>configuration | modules<br/>configuration |
---:|:---:|:---:|:---|
`apps.plugin`<br/>(external plugin for monitoring the process tree on Linux and FreeBSD)|`C`|`netdata.conf` section `[plugin:apps]`|Custom configuration for the processes to be monitored at `apps_groups.conf`
`freebsd.plugin`<br/>(internal plugin for monitoring FreeBSD system resources)|`C`|`netdata.conf` section `[plugin:freebsd]`|one section for each module `[plugin:freebsd:MODULE]`. Each module may provide additional sections in the form of `[plugin:freebsd:MODULE:SUBSECTION]`.
`cgroups.plugin`<br/>(internal plugin for monitoring Linux containers, VMs and systemd services)|`C`|`netdata.conf` section `[plugin:cgroups]`|N/A
`charts.d.plugin`<br/>(external plugin orchestrator for BASH modules)|`BASH`|`charts.d.conf`|a file for each module in `/etc/netdata/charts.d/`
`diskspace.plugin`<br/>(internal plugin for collecting Linux mount points usage)|`C`|`netdata.conf` section `[plugin:diskspace]`|N/A
`fping.plugin`<br/>(external plugin for collecting network latencies)|`C`|`fping.conf`|This plugin is a wrapper for the `fping` command.
`ioping.plugin`<br/>(external plugin for collecting disk latencies)|`C`|`ioping.conf`|This plugin is a wrapper for the `ioping` command.
`freeipmi.plugin`<br/>(external plugin for collecting IPMI h/w sensors)|`C`|`netdata.conf` section `[plugin:freeipmi]`
`nfacct.plugin`<br/>(external plugin for monitoring netfilter firewall and connection tracker)|`C`|`netdata.conf` section `[plugin:nfacct]`|N/A
`xenstat.plugin`<br/>(external plugin for monitoring XCP-ng and XenServer)|`C`|`netdata.conf` section `[plugin:xenstat]`|N/A
`perf.plugin`<br/>(external plugin for monitoring CPU performance on Linux)|`C`|`netdata.conf` section `[plugin:perf]`|N/A
`idlejitter.plugin`<br/>(internal plugin for monitoring CPU jitter)|`C`|N/A|N/A
`macos.plugin`<br/>(internal plugin for monitoring MacOS system resources)|`C`|`netdata.conf` section `[plugin:macos]`|one section for each module `[plugin:macos:MODULE]`. Each module may provide additional sections in the form of `[plugin:macos:MODULE:SUBSECTION]`.
`node.d.plugin`<br/>(external plugin orchestrator of node.js modules)|`node.js`|`node.d.conf`|a file for each module in `/etc/netdata/node.d/`.
`proc.plugin`<br/>(internal plugin for monitoring Linux system resources)|`C`|`netdata.conf` section `[plugin:proc]`|one section for each module `[plugin:proc:MODULE]`. Each module may provide additional sections in the form of `[plugin:proc:MODULE:SUBSECTION]`.
`python.d.plugin`<br/>(external plugin orchestrator for running python modules)|`python`<br/>v2 or v3<br/>both are supported|`python.d.conf`|a file for each module in `/etc/netdata/python.d/`.
`statsd.plugin`<br/>(internal plugin for collecting statsd metrics)|`C`|`netdata.conf` section `[statsd]`|Synthetic statsd charts can be configured with files in `/etc/netdata/statsd.d/`.
`tc.plugin`<br/>(internal plugin for collecting Linux traffic QoS)|`C`|`netdata.conf` section `[plugin:tc]`|The plugin runs an external helper called `tc-qos-helper.sh` to interface with the `tc` command. This helper supports a few additional options using `tc-qos-helper.conf`.


## writing data collection modules

You can add custom plugins following the [External Plugins Guide](../collectors/plugins.d/).

---

## available data collection modules

These are all the data collection plugins currently available.

### Web Servers

application|language|notes|
:---------:|:------:|:----|
apache|python<br/>v2 or v3|Connects to multiple apache servers (local or remote) to collect real-time performance metrics.<br/>&nbsp;<br/>Netdata plugin: [python.d.plugin](../collectors/python.d.plugin)<br/>plugin module: [apache.chart.py](../collectors/python.d.plugin/apache)<br/>configuration file: [python.d/apache.conf](../collectors/python.d.plugin/apache)|
apache|BASH<br/>Shell Script|Connects to an apache server (local or remote) to collect real-time performance metrics.<br/><br/>DEPRECATED IN FAVOR OF THE PYTHON ONE. It is still supplied only as an example module to shell scripting plugins.<br/>&nbsp;<br/>Netdata plugin: [charts.d.plugin](../collectors/charts.d.plugin#chartsdplugin)<br/>plugin module: [apache.chart.sh](../collectors/charts.d.plugin/apache)<br/>configuration file: [charts.d/apache.conf](../collectors/charts.d.plugin/apache)|
ipfs|python<br/>v2 or v3|Connects to multiple ipfs servers (local or remote) to collect real-time performance metrics.<br/>&nbsp;<br/>Netdata plugin: [python.d.plugin](../collectors/python.d.plugin)<br/>plugin module: [ipfs.chart.py](../collectors/python.d.plugin/ipfs)<br/>configuration file: [python.d/ipfs.conf](../collectors/python.d.plugin/ipfs)|
litespeed|python<br/>v2 or v3|reads the litespeed `rtreport` files to collect metrics.<br/>&nbsp;<br/>Netdata plugin: [python.d.plugin](../collectors/python.d.plugin)<br/>plugin module: [litespeed.chart.py](../collectors/python.d.plugin/litespeed)<br/>configuration file: [python.d/litespeed.conf](../collectors/python.d.plugin/litespeed)
nginx|python<br/>v2 or v3|Connects to multiple nginx servers (local or remote) to collect real-time performance metrics.<br/>&nbsp;<br/>Netdata plugin: [python.d.plugin](../collectors/python.d.plugin)<br/>plugin module: [nginx.chart.py](../collectors/python.d.plugin/nginx)<br/>configuration file: [python.d/nginx.conf](../collectors/python.d.plugin/nginx)|
nginx_plus|python<br/>v2 or v3|Connects to multiple nginx_plus servers (local or remote) to collect real-time performance metrics.<br/>&nbsp;<br/>Netdata plugin: [python.d.plugin](../collectors/python.d.plugin)<br/>plugin module: [nginx_plus.chart.py](../collectors/python.d.plugin/nginx_plus)<br/>configuration file: [python.d/nginx_plus.conf](../collectors/python.d.plugin/nginx_plus)|
nginx|BASH<br/>Shell Script|Connects to an nginx server (local or remote) to collect real-time performance metrics.<br/><br/>DEPRECATED IN FAVOR OF THE PYTHON ONE. It is still supplied only as an example module to shell scripting plugins.<br/>&nbsp;<br/>Netdata plugin: [charts.d.plugin](../collectors/charts.d.plugin#chartsdplugin)<br/>plugin module: [nginx.chart.sh](../collectors/charts.d.plugin/nginx)<br/>configuration file: [charts.d/nginx.conf](../collectors/charts.d.plugin/nginx)|
phpfpm|python<br/>v2 or v3|Connects to multiple phpfpm servers (local or remote) to collect real-time performance metrics.<br/>&nbsp;<br/>Netdata plugin: [python.d.plugin](../collectors/python.d.plugin)<br/>plugin module: [phpfpm.chart.py](../collectors/python.d.plugin/phpfpm)<br/>configuration file: [python.d/phpfpm.conf](../collectors/python.d.plugin/phpfpm)|
phpfpm|BASH<br/>Shell Script|Connects to one or more phpfpm servers (local or remote) to collect real-time performance metrics.<br/><br/>DEPRECATED IN FAVOR OF THE PYTHON ONE. It is still supplied only as an example module to shell scripting plugins.<br/>&nbsp;<br/>Netdata plugin: [charts.d.plugin](../collectors/charts.d.plugin#chartsdplugin)<br/>plugin module: [phpfpm.chart.sh](../collectors/charts.d.plugin/phpfpm)<br/>configuration file: [charts.d/phpfpm.conf](../collectors/charts.d.plugin/phpfpm)|
tomcat|python<br/>v2 or v3|Connects to multiple tomcat servers (local or remote) to collect real-time performance metrics.<br/>&nbsp;<br/>Netdata plugin: [python.d.plugin](../collectors/python.d.plugin)<br/>plugin module: [tomcat.chart.py](../collectors/python.d.plugin/tomcat)<br/>configuration file: [python.d/tomcat.conf](../collectors/python.d.plugin/tomcat)|
tomcat|BASH<br/>Shell Script|Connects to a tomcat server (local or remote) to collect real-time performance metrics.<br/><br/>DEPRECATED IN FAVOR OF THE PYTHON ONE. It is still supplied only as an example module to shell scripting plugins.<br/>&nbsp;<br/>Netdata plugin: [charts.d.plugin](../collectors/charts.d.plugin#chartsdplugin)<br/>plugin module: [tomcat.chart.sh](../collectors/charts.d.plugin/tomcat)<br/>configuration file: [charts.d/tomcat.conf](../collectors/charts.d.plugin/tomcat)|


---

### Web Log Parsers

application|language|notes|
:---------:|:------:|:----|
web_log|python<br/>v2 or v3|powerful plugin, capable of incrementally parsing any number of web server log files  <br/>&nbsp;<br/>Netdata plugin: [python.d.plugin](../collectors/python.d.plugin)<br/>plugin module: [web_log.chart.py](../collectors/python.d.plugin/web_log)<br/>configuration file: [python.d/web_log.conf](../collectors/python.d.plugin/web_log)|


---

### Database Servers

application|language|notes|
:---------:|:------:|:----|
couchdb|python<br/>v2 or v3|Connects to multiple couchdb servers (local or remote) to collect real-time performance metrics.<br/>&nbsp;<br/>Netdata plugin: [python.d.plugin](../collectors/python.d.plugin)<br/>plugin module: [couchdb.chart.py](../collectors/python.d.plugin/couchdb)<br/>configuration file: [python.d/couchdb.conf](../collectors/python.d.plugin/couchdb)|
memcached|python<br/>v2 or v3|Connects to multiple memcached servers (local or remote) to collect real-time performance metrics.<br/>&nbsp;<br/>Netdata plugin: [python.d.plugin](../collectors/python.d.plugin)<br/>plugin module: [memcached.chart.py](../collectors/python.d.plugin/memcached)<br/>configuration file: [python.d/memcached.conf](../collectors/python.d.plugin/memcached)|
mongodb|python<br/>v2 or v3|Connects to multiple `mongodb` servers (local or remote) to collect real-time performance metrics.<br/>&nbsp;<br/>Requires package `python-pymongo`.<br/>&nbsp;<br/>Netdata plugin: [python.d.plugin](../collectors/python.d.plugin)<br/>plugin module: [mongodb.chart.py](../collectors/python.d.plugin/mongodb)<br/>configuration file: [python.d/mongodb.conf](../collectors/python.d.plugin/mongodb)|
mysql<br/>mariadb|python<br/>v2 or v3|Connects to multiple mysql or mariadb servers (local or remote) to collect real-time performance metrics.<br/>&nbsp;<br/>Requires package `python-mysqldb` (faster and preferred), or `python-pymysql`. <br/>&nbsp;<br/>Netdata plugin: [python.d.plugin](../collectors/python.d.plugin)<br/>plugin module: [mysql.chart.py](../collectors/python.d.plugin/mysql)<br/>configuration file: [python.d/mysql.conf](../collectors/python.d.plugin/mysql)|
mysql<br/>mariadb|BASH<br/>Shell Script|Connects to multiple mysql or mariadb servers (local or remote) to collect real-time performance metrics.<br/><br/>DEPRECATED IN FAVOR OF THE PYTHON ONE. It is still supplied only as an example module to shell scripting plugins.<br/>&nbsp;<br/>Netdata plugin: [charts.d.plugin](../collectors/charts.d.plugin#chartsdplugin)<br/>plugin module: [mysql.chart.sh](../collectors/charts.d.plugin/mysql)<br/>configuration file: [charts.d/mysql.conf](../collectors/charts.d.plugin/mysql)|
postgres|python<br/>v2 or v3|Connects to multiple postgres servers (local or remote) to collect real-time performance metrics.<br/>&nbsp;<br/>Requires package `python-psycopg2`.<br/>&nbsp;<br/>Netdata plugin: [python.d.plugin](../collectors/python.d.plugin)<br/>plugin module: [postgres.chart.py](../collectors/python.d.plugin/postgres)<br/>configuration file: [python.d/postgres.conf](../collectors/python.d.plugin/postgres)|
redis|python<br/>v2 or v3|Connects to multiple redis servers (local or remote) to collect real-time performance metrics.<br/>&nbsp;<br/>Netdata plugin: [python.d.plugin](../collectors/python.d.plugin)<br/>plugin module: [redis.chart.py](../collectors/python.d.plugin/redis)<br/>configuration file: [python.d/redis.conf](../collectors/python.d.plugin/redis)|
rethinkdb|python<br/>v2 or v3|Connects to multiple rethinkdb servers (local or remote) to collect real-time metrics.<br/>&nbsp;<br/>Netdata plugin: [python.d.plugin](../collectors/python.d.plugin)<br/>plugin module: [rethinkdb.chart.py](../collectors/python.d.plugin/rethinkdbs)<br/>configuration file: [python.d/rethinkdb.conf](../collectors/python.d.plugin/rethinkdbs)|


---

### Social Sharing Servers

application|language|notes|
:---------:|:------:|:----|
retroshare|python<br/>v2 or v3|Connects to multiple retroshare servers (local or remote) to collect real-time performance metrics.<br/>&nbsp;<br/>netdata plugin: [python.d.plugin](../collectors/python.d.plugin)<br/>plugin module: [retroshare.chart.py](../collectors/python.d.plugin/retroshare)<br/>configuration file: [python.d/retroshare.conf](../collectors/python.d.plugin/retroshare)|


---

### Proxy Servers

application|language|notes|
:---------:|:------:|:----|
squid|python<br/>v2 or v3|Connects to multiple squid servers (local or remote) to collect real-time performance metrics.<br/>&nbsp;<br/>netdata plugin: [python.d.plugin](../collectors/python.d.plugin)<br/>plugin module: [squid.chart.py](../collectors/python.d.plugin/squid)<br/>configuration file: [python.d/squid.conf](../collectors/python.d.plugin/squid)|
squid|BASH<br/>Shell Script|Connects to a squid server (local or remote) to collect real-time performance metrics.<br/><br/>DEPRECATED IN FAVOR OF THE PYTHON ONE. It is still supplied only as an example module to shell scripting plugins.<br/>&nbsp;<br/>Netdata plugin: [charts.d.plugin](../collectors/charts.d.plugin#chartsdplugin)<br/>plugin module: [squid.chart.sh](../collectors/charts.d.plugin/squid)<br/>configuration file: [charts.d/squid.conf](../collectors/charts.d.plugin/squid)|


---

### HTTP Accelerators

application|language|notes|
:---------:|:------:|:----|
varnish|python<br/>v2 or v3|Uses the varnishstat command to provide varnish cache statistics (client metrics, cache perfomance, thread-related metrics, backend health, memory usage etc.).<br/>&nbsp;<br/>Netdata plugin: [python.d.plugin](../collectors/python.d.plugin)<br/>plugin module: [varnish.chart.py](../collectors/python.d.plugin/varnish)<br/>configuration file: [python.d/varnish.conf](../collectors/python.d.plugin/varnish)|


---

### Search Engines

application|language|notes|
:---------:|:------:|:----|
elasticsearch|python<br/>v2 or v3|Monitor elasticsearch performance and health metrics.<br/>&nbsp;<br/>Netdata plugin: [python.d.plugin](../collectors/python.d.plugin)<br/>plugin module: [elasticsearch.chart.py](../collectors/python.d.plugin/elasticsearch)<br/>configuration file: [python.d/elasticsearch.conf](../collectors/python.d.plugin/elasticsearch)|


---

### Name Servers

application|language|notes|
:---------:|:------:|:----|
named|node.js|Connects to multiple named (ISC-Bind) servers (local or remote) to collect real-time performance metrics. All versions of bind after 9.9.10 are supported.<br/>&nbsp;<br/>Netdata plugin: [node.d.plugin](../collectors/node.d.plugin#nodedplugin)<br/>plugin module: [named.node.js](../collectors/node.d.plugin/named)<br/>configuration file: [node.d/named.conf](../collectors/node.d.plugin/named)|
bind_rndc|python<br/>v2 or v3|Parses named.stats dump file to collect real-time performance metrics. All versions of bind after 9.6 are supported.<br/>&nbsp;<br/>Netdata plugin: [python.d.plugin](../collectors/python.d.plugin)<br/>plugin module: [bind_rndc.chart.py](../collectors/python.d.plugin/bind_rndc)<br/>configuration file: [python.d/bind_rndc.conf](../collectors/python.d.plugin/bind_rndc)|
nsd|python<br/>v2 or v3|Charts the nsd received queries and zones.<br/>&nbsp;<br/>Netdata plugin: [python.d.plugin](../collectors/python.d.plugin)<br/>plugin module: [nsd.chart.py](../collectors/python.d.plugin/nsd)<br/>configuration file: [python.d/nsd.conf](../collectors/python.d.plugin/nsd)
powerdns|python<br/>v2 or v3|Monitors powerdns performance and health metrics <br/>&nbsp;<br/>Netdata plugin: [python.d.plugin](../collectors/python.d.plugin)<br/>plugin module: [powerdns.chart.py](../collectors/python.d.plugin/powerdns)<br/>configuration file: [python.d/powerdns.conf](../collectors/python.d.plugin/powerdns)|
dnsdist|python<br/>v2 or v3|Monitors dnsdist performance and health metrics <br/>&nbsp;<br/>Netdata plugin: [python.d.plugin](../collectors/python.d.plugin)<br/>plugin module: [dnsdist.chart.py](../collectors/python.d.plugin/dnsdist)<br/>configuration file: [python.d/dnsdist.conf](../collectors/python.d.plugin/dnsdist)|
unbound|python<br/>v2 or v3|Monitors Unbound performance and resource usage metrics <br/>&nbsp;<br/>Netdata plugin: [python.d.plugin](../collectors/python.d.plugin)<br/>plugin module: [unbound.chart.py](../collectors/python.d.plugin/unbound)<br/>configuration file: [python.d/unbound.conf](../collectors/python.d.plugin/unbound)|


---

### DHCP Servers

application|language|notes|
:---------:|:------:|:----|
isc dhcp|python<br/>v2 or v3|Monitor lease database to show all active leases.<br/>&nbsp;<br/>Python v2 requires package `python-ipaddress`.<br/>&nbsp;<br/>Netdata plugin: [python.d.plugin](../collectors/python.d.plugin)<br/>plugin module: [isc-dhcpd.chart.py](../collectors/python.d.plugin/isc_dhcpd)<br/>configuration file: [python.d/isc-dhcpd.conf](../collectors/python.d.plugin/isc_dhcpd)|


---

### Load Balancers

application|language|notes|
:---------:|:------:|:----|
haproxy|python<br/>v2 or v3|Monitor frontend, backend and health metrics.<br/>&nbsp;<br/>Netdata plugin: [python.d.plugin](../collectors/python.d.plugin)<br/>plugin module: [haproxy.chart.py](../collectors/python.d.plugin/haproxy)<br/>configuration file: [python.d/haproxy.conf](../collectors/python.d.plugin/haproxy)|
traefik|python<br/>v2 or v3|Connects to multiple traefik instances (local or remote) to collect API metrics (response status code, response time, average response time and server uptime).<br/>&nbsp;<br/>Netdata plugin: [python.d.plugin](../collectors/python.d.plugin)<br/>plugin module: [traefik.chart.py](../collectors/python.d.plugin/traefik)<br/>configuration file: [python.d/traefik.conf](../collectors/python.d.plugin/traefik)|

---

### Message Brokers

application|language|notes|
:---------:|:------:|:----|
rabbitmq|python<br/>v2 or v3|Monitor rabbitmq performance and health metrics.<br/>&nbsp;<br/>Netdata plugin: [python.d.plugin](../collectors/python.d.plugin)<br/>plugin module: [rabbitmq.chart.py](../collectors/python.d.plugin/rabbitmq)<br/>configuration file: [python.d/rabbitmq.conf](../collectors/python.d.plugin/rabbitmq)|
beanstalkd|python<br/>v2 or v3|Provides server and tube level statistics.<br/>&nbsp;<br/>Requires beanstalkc python package (`pip install beanstalkc` or install package `python-beanstalkc`, which also installs `python-yaml`).<br/>&nbsp;<br/>Netdata plugin: [python.d.plugin](../collectors/python.d.plugin)<br/>plugin module: [beanstalk.chart.py](../collectors/python.d.plugin/beanstalk)<br/>configuration file: [python.d/beanstalk.conf](../collectors/python.d.plugin/beanstalk)|


---

### UPS

application|language|notes|
:---------:|:------:|:----|
apcupsd|BASH<br/>Shell Script|Connects to an apcupsd server to collect real-time statistics of an APC UPS.<br/>&nbsp;<br/>Netdata plugin: [charts.d.plugin](../collectors/charts.d.plugin#chartsdplugin)<br/>plugin module: [apcupsd.chart.sh](../collectors/charts.d.plugin/apcupsd)<br/>configuration file: [charts.d/apcupsd.conf](../collectors/charts.d.plugin/apcupsd)|
nut|BASH<br/>Shell Script|Connects to a nut server (upsd) to collect real-time UPS statistics.<br/>&nbsp;<br/>Netdata plugin: [charts.d.plugin](../collectors/charts.d.plugin#chartsdplugin)<br/>plugin module: [nut.chart.sh](../collectors/charts.d.plugin/nut)<br/>configuration file: [charts.d/nut.conf](../collectors/charts.d.plugin/nut)|


---

### RAID

application|language|notes|
:---------:|:------:|:----|
megacli|python<br/>v2 or v3|Collects adapter, physical drives and battery stats..<br/>&nbsp;<br/>Netdata plugin: [python.d.plugin](../collectors/python.d.plugin)<br/>plugin module: [megacli.chart.py](../collectors/python.d.plugin/megacli)<br/>configuration file: [python.d/megacli.conf](../collectors/python.d.plugin/megacli)|

---

### Mail Servers

application|language|notes|
:---------:|:------:|:----|
dovecot|python<br/>v2 or v3|Connects to multiple dovecot servers (local or remote) to collect real-time performance metrics.<br/>&nbsp;<br/>Netdata plugin: [python.d.plugin](../collectors/python.d.plugin)<br/>plugin module: [dovecot.chart.py](../collectors/python.d.plugin/dovecot)<br/>configuration file: [python.d/dovecot.conf](../collectors/python.d.plugin/dovecot)|
exim|python<br/>v2 or v3|Charts the exim queue size.<br/>&nbsp;<br/>Netdata plugin: [python.d.plugin](../collectors/python.d.plugin)<br/>plugin module: [exim.chart.py](../collectors/python.d.plugin/exim)<br/>configuration file: [python.d/exim.conf](../collectors/python.d.plugin/exim)|
exim|BASH<br/>Shell Script|Charts the exim queue size.<br/><br/>DEPRECATED IN FAVOR OF THE PYTHON ONE. It is still supplied only as an example module to shell scripting plugins.<br/>&nbsp;<br/>Netdata plugin: [charts.d.plugin](../collectors/charts.d.plugin#chartsdplugin)<br/>plugin module: [exim.chart.sh](../collectors/charts.d.plugin/exim)<br/>configuration file: [charts.d/exim.conf](../collectors/charts.d.plugin/exim)|
postfix|python<br/>v2 or v3|Charts the postfix queue size (supports multiple queues).<br/>&nbsp;<br/>Netdata plugin: [python.d.plugin](../collectors/python.d.plugin)<br/>plugin module: [postfix.chart.py](../collectors/python.d.plugin/postfix)<br/>configuration file: [python.d/postfix.conf](../collectors/python.d.plugin/postfix)|
postfix|BASH<br/>Shell Script|Charts the postfix queue size.<br/><br/>DEPRECATED IN FAVOR OF THE PYTHON ONE. It is still supplied only as an example module to shell scripting plugins.<br/>&nbsp;<br/>Netdata plugin: [charts.d.plugin](../collectors/charts.d.plugin#chartsdplugin)<br/>plugin module: [postfix.chart.sh](../collectors/charts.d.plugin/postfix)<br/>configuration file: [charts.d/postfix.conf](../collectors/charts.d.plugin/postfix)|


---

### File Servers

application|language|notes|
:---------:|:------:|:----|
NFS Client|`C`|This is handled entirely by the Netdata daemon.<br/>&nbsp;<br/>Configuration: `netdata.conf`, section `[plugin:proc:/proc/net/rpc/nfs]`.
NFS Server|`C`|This is handled entirely by the netdata daemon.<br/>&nbsp;<br/>Configuration: `netdata.conf`, section `[plugin:proc:/proc/net/rpc/nfsd]`.
samba|python<br/>v2 or v3|Performance metrics of Samba SMB2 file sharing.<br/>&nbsp;<br/>documentation page: [python.d.plugin module samba](../collectors/python.d.plugin/samba)<br/>netdata plugin: [python.d.plugin](../collectors/python.d.plugin)<br/>plugin module: [samba.chart.py](../collectors/python.d.plugin/samba)<br/>configuration file: [python.d/samba.conf](../collectors/python.d.plugin/samba)|

---

### Print Servers

application|language|notes|
:---------:|:------:|:----|
CUPS|C|Charts metrics of printers, jobs and other cups destinations.<br/>&nbsp;<br/>netdata plugin: [cups.plugin](../collectors/cups.plugin)

---

### Hypervisors

application|language|notes|
:---------:|:------:|:----|
xenstat|C|Collects host and domain statistics for XenServer or XCP-ng hypervisors.<br/>&nbsp;<br/>netdata plugin: [xenstat.plugin](../collectors/xenstat.plugin)

---

### System

application|language|notes|
:---------:|:------:|:----|
apps|C|`apps.plugin` collects resource usage statistics for all processes running in the system. It groups the entire process tree and reports dozens of metrics for CPU utilization, memory footprint, disk I/O, swap memory, network connections, open files and sockets, etc. It reports metrics for application groups, users and user groups.<br/>&nbsp;<br/>[Documentation of `apps.plugin`](../collectors/apps.plugin/).<br/>&nbsp;<br/>Netdata plugin: [`apps_plugin.c`](../collectors/apps.plugin)<br/>configuration file: [`apps_groups.conf`](../collectors/apps.plugin)|
ioping|C|Charts disk latency statistics for a directory/file/device, using the `ioping` command. A recent (probably unreleased) version of ioping is required. The plugin supplied can install it in `/usr/local`.<br/>&nbsp;<br/>Netdata plugin: [ioping.plugin](../collectors/ioping.plugin) (this is a shell wrapper to start ioping - once ioping is started, Netdata and ioping communicate directly - it can also install the right version of ioping)<br/>configuration file: [ioping.conf](../collectors/ioping.plugin)|
perf|C|`perf.plugin` collects CPU performance metrics using hardware performance monitoring units (PMU).<br/>&nbsp;<br/>[Documentation of `perf.plugin`](../collectors/perf.plugin/).<br/>&nbsp;<br/>Netdata plugin: [`perf_plugin.c`](../collectors/perf.plugin)|
cpu_apps|BASH<br/>Shell Script|Collects the CPU utilization of select apps.<br/><br/>DEPRECATED IN FAVOR OF `apps.plugin`. It is still supplied only as an example module to shell scripting plugins.<br/>&nbsp;<br/>Netdata plugin: [charts.d.plugin](../collectors/charts.d.plugin#chartsdplugin)<br/>plugin module: [cpu_apps.chart.sh](../collectors/charts.d.plugin/cpu_apps)<br/>configuration file: [charts.d/cpu_apps.conf](../collectors/charts.d.plugin/cpu_apps)|
load_average|BASH<br/>Shell Script|Collects the current system load average.<br/><br/>DEPRECATED IN FAVOR OF THE NETDATA INTERNAL ONE. It is still supplied only as an example module to shell scripting plugins.<br/>&nbsp;<br/>Netdata plugin: [charts.d.plugin](../collectors/charts.d.plugin#chartsdplugin)<br/>plugin module: [load_average.chart.sh](../collectors/charts.d.plugin/load_average)<br/>configuration file: [charts.d/load_average.conf](../collectors/charts.d.plugin/load_average)|
mem_apps|BASH<br/>Shell Script|Collects the memory footprint of select applications.<br/><br/>DEPRECATED IN FAVOR OF `apps.plugin`. It is still supplied only as an example module to shell scripting plugins.<br/>&nbsp;<br/>Netdata plugin: [charts.d.plugin](../collectors/charts.d.plugin#chartsdplugin)<br/>plugin module: [mem_apps.chart.sh](../collectors/charts.d.plugin/mem_apps)<br/>configuration file: [charts.d/mem_apps.conf](../collectors/charts.d.plugin/mem_apps)|


---

### Sensors

application|language|notes|
:---------:|:------:|:----|
cpufreq|BASH<br/>Shell Script|Collects current CPU frequency from `/sys/devices`.<br/><br/>DEPRECATED IN FAVOR OF THE PYTHON ONE. It is still supplied only as an example module to shell scripting plugins.<br/>&nbsp;<br/>Netdata plugin: [charts.d.plugin](../collectors/charts.d.plugin#chartsdplugin)<br/>plugin module: [cpufreq.chart.sh](../collectors/charts.d.plugin/cpufreq)<br/>configuration file: [charts.d/cpufreq.conf](../collectors/charts.d.plugin/cpufreq)|
IPMI|C|Collects temperatures, voltages, currents, power, fans and `SEL` events from IPMI using `libipmimonitoring`.<br/>Check [Monitoring IPMI](../collectors/freeipmi.plugin/) for more information<br/>&nbsp;<br/>Netdata plugin: [freeipmi.plugin](../collectors/freeipmi.plugin)<br/>configuration file: none required - to enable it, compile/install Netdata with `--enable-plugin-freeipmi`|
hddtemp|python<br/>v2 or v3|Connects to multiple hddtemp servers (local or remote) to collect real-time performance metrics.<br/>&nbsp;<br/>Netdata plugin: [python.d.plugin](../collectors/python.d.plugin)<br/>plugin module: [hddtemp.chart.py](../collectors/python.d.plugin/hddtemp)<br/>configuration file: [python.d/hddtemp.conf](../collectors/python.d.plugin/hddtemp)|
hddtemp|BASH<br/>Shell Script|Connects to a hddtemp server (local or remote) to collect real-time performance metrics.<br/><br/>DEPRECATED IN FAVOR OF THE PYTHON ONE. It is still supplied only as an example module to shell scripting plugins.<br/>&nbsp;<br/>Netdata plugin: [charts.d.plugin](../collectors/charts.d.plugin#chartsdplugin)<br/>plugin module: [hddtemp.chart.sh](../collectors/charts.d.plugin/hddtemp)<br/>configuration file: [charts.d/hddtemp.conf](../collectors/charts.d.plugin/hddtemp)|
sensors|BASH<br/>Shell Script|Collects sensors values from files in `/sys`.<br/><br/>DEPRECATED IN FAVOR OF THE PYTHON ONE. It is still supplied only as an example module to shell scripting plugins.<br/>&nbsp;<br/>Netdata plugin: [charts.d.plugin](../collectors/charts.d.plugin#chartsdplugin)<br/>plugin module: [sensors.chart.sh](../collectors/charts.d.plugin/sensors)<br/>configuration file: [charts.d/sensors.conf](../collectors/charts.d.plugin/sensors)|
sensors|python<br/>v2 or v3|Uses `lm-sensors` to collect sensor data.<br/>&nbsp;<br/>Netdata plugin: [python.d.plugin](../collectors/python.d.plugin)<br/>plugin module: [sensors.chart.py](../collectors/python.d.plugin/sensors)<br/>configuration file: [python.d/sensors.conf](../collectors/python.d.plugin/sensors)|
smartd_log|python<br/>v2 or v3|Collects the S.M.A.R.T attributes from `smartd` log files.<br/>&nbsp;<br/>Netdata plugin: [python.d.plugin](../collectors/python.d.plugin)<br/>plugin module: [smartd_log.chart.py](../collectors/python.d.plugin/smartd_log)<br/>configuration file: [python.d/smartd_log.conf](../collectors/python.d.plugin/smartd_log)|
w1sensor|python<br/>v2 or v3|Collects data from connected 1-Wire sensors.<br/>&nbsp;<br/>Netdata plugin: [python.d.plugin](../collectors/python.d.plugin)<br/>plugin module: [w1sensor.chart.py](../collectors/python.d.plugin/w1sensor)<br/>configuration file: [python.d/w1sensor.conf](../collectors/python.d.plugin/w1sensor)|


---

### Network

application|language|notes|
:---------:|:------:|:----|
ap|BASH<br/>Shell Script|Uses the `iw` command to provide statistics of wireless clients connected to a wireless access point running on this host (works well with `hostapd`).<br/>&nbsp;<br/>Netdata plugin: [charts.d.plugin](../collectors/charts.d.plugin#chartsdplugin)<br/>plugin module: [ap.chart.sh](../collectors/charts.d.plugin/ap)<br/>configuration file: [charts.d/ap.conf](../collectors/charts.d.plugin/ap)|
fping|C|Charts network latency statistics for any number of nodes, using the `fping` command. A recent (probably unreleased) version of fping is required. The plugin supplied can install it in `/usr/local`.<br/>&nbsp;<br/>Netdata plugin: [fping.plugin](../collectors/fping.plugin) (this is a shell wrapper to start fping - once fping is started, Netdata and fping communicate directly - it can also install the right version of fping)<br/>configuration file: [fping.conf](../collectors/fping.plugin)|
snmp|node.js|Connects to multiple snmp servers to collect real-time performance metrics.<br/>&nbsp;<br/>Netdata plugin: [node.d.plugin](../collectors/node.d.plugin#nodedplugin)<br/>plugin module: [snmp.node.js](../collectors/node.d.plugin/snmp)<br/>configuration file: [node.d/snmp.conf](../collectors/node.d.plugin/snmp)|
nfacct|C|collects netfilter firewall, connection tracker and accounting metrics using `libmnl` and `libnetfilter_acct`|
dns_query_time|python<br/>v2 or v3|Provides DNS query time statistics.<br/>&nbsp;<br/>Requires package `dnspython` (`pip install dnspython` or install package `python-dnspython`).<br/>&nbsp;<br/>Netdata plugin: [python.d.plugin](../collectors/python.d.plugin)<br/>plugin module: [dns_query_time.chart.py](../collectors/python.d.plugin/dns_query_time)<br/>configuration file: [python.d/dns_query_time.conf](../collectors/python.d.plugin/dns_query_time)|
http|python<br />v2 or v3|Monitors a generic web page for status code and returned content in HTML
port|ptyhon<br />v2 or v3|Checks if a generic TCP port for its availability and response time


---

### Time Servers

application|language|notes|
:---------:|:------:|:----|
chrony|python<br/>v2 or v3|Uses the chronyc command to provide chrony statistics (Frequency, Last offset, RMS offset, Residual freq, Root delay, Root dispersion, Skew, System time).<br/>&nbsp;<br/>Netdata plugin: [python.d.plugin](../collectors/python.d.plugin)<br/>plugin module: [chrony.chart.py](../collectors/python.d.plugin/chrony)<br/>configuration file: [python.d/chrony.conf](../collectors/python.d.plugin/chrony)|
ntpd|python<br/>v2 or v3|Connects to multiple ntpd servers (local or remote) to provide statistics of system variables and optional also peer variables (if enabled in the configuration).<br/>&nbsp;<br/>Netdata plugin: [python.d.plugin](../collectors/python.d.plugin)<br/>plugin module: [ntpd.chart.py](../collectors/python.d.plugin/ntpd)<br/>configuration file: [python.d/ntpd.conf](../collectors/python.d.plugin/ntpd)|


---

### Security

application|language|notes|
:---------:|:------:|:----|
freeradius|python<br/>v2 or v3|Uses the radclient command to provide freeradius statistics (authentication, accounting, proxy-authentication, proxy-accounting).<br/>&nbsp;<br/>Netdata plugin: [python.d.plugin](../collectors/python.d.plugin)<br/>plugin module: [freeradius.chart.py](../collectors/python.d.plugin/freeradius)<br/>configuration file: [python.d/freeradius.conf](../collectors/python.d.plugin/freeradius)|
openvpn|python<br/>v2 or v3|All data from openvpn-status.log in your dashboard! <br/>&nbsp;<br/>Netdata plugin: [python.d.plugin](../collectors/python.d.plugin)<br/>plugin module: [ovpn_status_log.chart.py](../collectors/python.d.plugin/ovpn_status_log)<br/>configuration file: [python.d/ovpn_status_log.conf](../collectors/python.d.plugin/ovpn_status_log)|
fail2ban|python<br/>v2 or v3|Monitor fail2ban log file to show all bans for all active jails <br/>&nbsp;<br/>Netdata plugin: [python.d.plugin](../collectors/python.d.plugin)<br/>plugin module: [fail2ban.chart.py](../collectors/python.d.plugin/fail2ban)<br/>configuration file: [python.d/fail2ban.conf](../collectors/python.d.plugin/fail2ban)|


---

### Telephony Servers

application|language|notes|
:---------:|:------:|:----|
opensips|BASH<br/>Shell Script|Connects to an opensips server (local only) to collect real-time performance metrics.<br/>&nbsp;<br/>Netdata plugin: [charts.d.plugin](../collectors/charts.d.plugin#chartsdplugin)<br/>plugin module: [opensips.chart.sh](../collectors/charts.d.plugin/opensips)<br/>configuration file: [charts.d/opensips.conf](../collectors/charts.d.plugin/opensips)|


---

### Go applications

application|language|notes|
:---------:|:------:|:----|
go_expvar|python<br/>v2 or v3|Parses metrics exposed by applications written in the Go programming language using the [expvar package](https://golang.org/pkg/expvar/).<br/>&nbsp;<br/>Netdata plugin: [python.d.plugin](../collectors/python.d.plugin)<br/>plugin module: [go_expvar.chart.py](../collectors/python.d.plugin/go_expvar)<br/>configuration file: [python.d/go_expvar.conf](../collectors/python.d.plugin/go_expvar)<br/>documentation: [Monitoring Go Applications](../collectors/python.d.plugin/go_expvar/)|


---

### Household Appliances

application|language|notes|
:---------:|:------:|:----|
sma_webbox|node.js|Connects to multiple remote SMA webboxes to collect real-time performance metrics of the photovoltaic (solar) power generation.<br/>&nbsp;<br/>Netdata plugin: [node.d.plugin](../collectors/node.d.plugin#nodedplugin)<br/>plugin module: [sma_webbox.node.js](../collectors/node.d.plugin/sma_webbox)<br/>configuration file: [node.d/sma_webbox.conf](../collectors/node.d.plugin/sma_webbox)|
fronius|node.js|Connects to multiple remote Fronius Symo servers to collect real-time performance metrics of the photovoltaic (solar) power generation.<br/>&nbsp;<br/>Netdata plugin: [node.d.plugin](../collectors/node.d.plugin#nodedplugin)<br/>plugin module: [fronius.node.js](../collectors/node.d.plugin/fronius)<br/>configuration file: [node.d/fronius.conf](../collectors/node.d.plugin/fronius)|
stiebeleltron|node.js|Collects the temperatures and other metrics from your Stiebel Eltron heating system using their Internet Service Gateway (ISG web).<br/>&nbsp;<br/>Netdata plugin: [node.d.plugin](../collectors/node.d.plugin#nodedplugin)<br/>plugin module: [stiebeleltron.node.js](../collectors/node.d.plugin/stiebeleltron)<br/>configuration file: [node.d/stiebeleltron.conf](../collectors/node.d.plugin/stiebeleltron)|


---

### Java Processes

application|language|notes|
:---------:|:------:|:----|
Spring Boot Application|java|Monitors running Java [Spring Boot](https://spring.io/) applications that expose their metrics with the use of the **Spring Boot Actuator** included in Spring Boot library.<br/>&nbsp;<br/>Netdata plugin: [python.d.plugin](../collectors/python.d.plugin)<br/>plugin module: [springboot](../collectors/python.d.plugin/springboot)<br/>configuration file: [python.d/springboot.conf](../collectors/python.d.plugin/springboot)


---

### Provisioning Systems

application|language|notes|
:---------:|:------:|:----|
puppet|python<br/>v2 or v3|Connects to multiple Puppet Server and Puppet DB instances (local or remote) to collect real-time status metrics.<br/>&nbsp;<br/>Netdata plugin: [python.d.plugin](../collectors/python.d.plugin)<br/>plugin module: [puppet.chart.py](../collectors/python.d.plugin/puppet)<br/>configuration file: [python.d/puppet.conf](../collectors/python.d.plugin/puppet)|

---

### Game Servers

application|language|notes|
:---------:|:------:|:----|
SpigotMC|Python<br/>v2 or v3|Monitors Spigot Minecraft server ticks per second and number of online players using the Minecraft remote console.<br/>&nbsp;<br/>Netdata plugin: [python.d.plugin](../collectors/python.d.plugin)<br/>plugin module: [spigotmc.chart.py](../collectors/python.d.plugin/spigotmc)<br/>configuration file: [python.d/spigotmc.conf](../collectors/python.d.plugin/spigotmc)|

---

### Distributed Computing Clients

application|language|notes|
:---------:|:------:|:----|
BOINC|Python<br/>v2 or v3|Monitors task states for local and remote BOINC client software using the remote GUI RPC interface.  Also provides alarms for a handful of error conditions.  Requires manual configuration<br/>&nbsp;<br/>Netdata plugin: [python.d.plugin](../collectors/python.d.plugin)<br/>plugin module: [boinc.chart.py](../collectors/python.d.plugin/boinc)<br/>configuration file: [python.d/boinc.conf](../collectors/python.d.plugin/boinc)|

---

### Skeleton Plugins

application|language|notes|
:---------:|:------:|:----|
example|BASH<br/>Shell Script|Skeleton plugin in BASH.<br/><br/>DEPRECATED IN FAVOR OF THE PYTHON ONE. It is still supplied only as an example module to shell scripting plugins.<br/>&nbsp;<br/>Netdata plugin: [charts.d.plugin](../collectors/charts.d.plugin#chartsdplugin)<br/>plugin module: [example.chart.sh](../collectors/charts.d.plugin/example)<br/>configuration file: [charts.d/example.conf](../collectors/charts.d.plugin/example)|
example|python<br/>v2 or v3|Skeleton plugin in Python.<br/>&nbsp;<br/>Netdata plugin: [python.d.plugin](../collectors/python.d.plugin)<br/>plugin module: [example.chart.py](../collectors/python.d.plugin/example)<br/>configuration file: [python.d/example.conf](../collectors/python.d.plugin/example)|

[![analytics](https://www.google-analytics.com/collect?v=1&aip=1&t=pageview&_s=1&ds=github&dr=https%3A%2F%2Fgithub.com%2Fnetdata%2Fnetdata&dl=https%3A%2F%2Fmy-netdata.io%2Fgithub%2Fdocs%2FAdd-more-charts-to-netdata&_u=MAC~&cid=5792dfd7-8dc4-476b-af31-da2fdb9f93d2&tid=UA-64295674-3)]()
