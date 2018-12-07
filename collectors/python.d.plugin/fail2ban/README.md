# fail2ban

Module monitor fail2ban log file to show all bans for all active jails

**Requirements:**
 * fail2ban.log file MUST BE readable by netdata (A good idea is to add  **create 0640 root netdata** to fail2ban conf at logrotate.d)

It produces one chart with multiple lines (one line per jail)

### configuration

Sample:

```yaml
local:
 log_path: '/var/log/fail2ban.log'
 conf_path: '/etc/fail2ban/jail.local'
 exclude: 'dropbear apache'
```
If no configuration is given, module will attempt to read log file at `/var/log/fail2ban.log` and conf file at `/etc/fail2ban/jail.local`.
If conf file is not found default jail is `ssh`.

---

[![analytics](https://www.google-analytics.com/collect?v=1&aip=1&t=pageview&_s=1&ds=github&dr=https%3A%2F%2Fgithub.com%2Fnetdata%2Fnetdata&dl=https%3A%2F%2Fmy-netdata.io%2Fgithub%2Fcollectors%2Fpython.d.plugin%2Ffail2ban%2FREADME&_u=MAC~&cid=5792dfd7-8dc4-476b-af31-da2fdb9f93d2&tid=UA-64295674-3)]()
