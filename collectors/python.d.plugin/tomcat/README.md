# tomcat

Present tomcat containers memory utilization.

Charts:

1. **Requests** per second
 * accesses

2. **Volume** in KB/s
 * volume

3. **Threads**
 * current
 * busy

4. **JVM Free Memory** in MB
 * jvm

### configuration

```yaml
localhost:
  name : 'local'
  url  : 'http://127.0.0.1:8080/manager/status?XML=true'
  user : 'tomcat_username'
  pass : 'secret_tomcat_password'
```

Without configuration, module attempts to connect to `http://localhost:8080/manager/status?XML=true`, without any credentials.
So it will probably fail.

---

[![analytics](https://www.google-analytics.com/collect?v=1&aip=1&t=pageview&_s=1&ds=github&dr=https%3A%2F%2Fgithub.com%2Fnetdata%2Fnetdata&dl=https%3A%2F%2Fmy-netdata.io%2Fgithub%2Fcollectors%2Fpython.d.plugin%2Ftomcat%2FREADME&_u=MAC~&cid=5792dfd7-8dc4-476b-af31-da2fdb9f93d2&tid=UA-64295674-3)]()
