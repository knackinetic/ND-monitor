# megacli

Module collects adapter, physical drives and battery stats.

**Requirements:**
 * `megacli` program
 * `sudo` program
 * `netdata` user needs to be able to be able to sudo the `megacli` program without password

To grab stats it executes:
 * `sudo -n megacli -LDPDInfo -aAll`
 * `sudo -n megacli -AdpBbuCmd -a0`


It produces:

1. **Adapter State**

2. **Physical Drives Media Errors**

3. **Physical Drives Predictive Failures**

4. **Battery Relative State of Charge**

5. **Battery Cycle Count**

### prerequisite
This module uses `megacli` which can only be executed by root.  It uses
`sudo` and assumes that it is configured such that the `netdata` user can
execute `megacli` as root without password.

Add to `sudoers`:

    netdata ALL=(root)       NOPASSWD: /path/to/megacli

### configuration

**megacli** is disabled by default. Should be explicitly enabled in `python.d.conf`.
```yaml
megacli: yes
```

Battery stats disabled by default. To enable them modify `megacli.conf`.
```yaml
do_battery: yes
```

---

[![analytics](https://www.google-analytics.com/collect?v=1&aip=1&t=pageview&_s=1&ds=github&dr=https%3A%2F%2Fgithub.com%2Fnetdata%2Fnetdata&dl=https%3A%2F%2Fmy-netdata.io%2Fgithub%2Fcollectors%2Fpython.d.plugin%2Fmegacli%2FREADME&_u=MAC~&cid=5792dfd7-8dc4-476b-af31-da2fdb9f93d2&tid=UA-64295674-3)]()
