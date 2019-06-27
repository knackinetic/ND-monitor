# mysql

Module monitors one or more mysql servers

**Requirements:**
 * python library [MySQLdb](https://github.com/PyMySQL/mysqlclient-python) (faster) or [PyMySQL](https://github.com/PyMySQL/PyMySQL) (slower)

It will produce following charts (if data is available):

1. **Bandwidth** in kilobits/s
 * in
 * out

2. **Queries** in queries/sec
 * queries
 * questions
 * slow queries

3. **Queries By Type** in queries/s
 * select
 * delete
 * update
 * insert
 * cache hits
 * replace

4. **Handlerse** in handlers/s
 * commit
 * delete
 * prepare
 * read first
 * read key
 * read next
 * read prev
 * read rnd
 * read rnd next
 * rollback
 * savepoint
 * savepoint rollback
 * update
 * write

4. **Table Locks** in locks/s
 * immediate
 * waited

5. **Table Select Join Issuess** in joins/s
 * full join
 * full range join
 * range
 * range check
 * scan

6. **Table Sort Issuess** in joins/s
 * merge passes
 * range
 * scan

7. **Tmp Operations** in created/s
 * disk tables
 * files
 * tables

8. **Connections** in connections/s
 * all
 * aborted

9. **Connections Active** in connections/s
 * active
 * limit
 * max active

10. **Binlog Cache** in threads
 * disk
 * all

11. **Threads** in transactions/s
 * connected
 * cached
 * running

12. **Threads Creation Rate** in threads/s
 * created

13. **Threads Cache Misses** in misses
 * misses

14. **InnoDB I/O Bandwidth** in KiB/s
 * read
 * write

15. **InnoDB I/O Operations** in operations/s
 * reads
 * writes
 * fsyncs

16. **InnoDB Pending I/O Operations** in operations/s
 * reads
 * writes
 * fsyncs

17. **InnoDB Log Operations** in operations/s
 * waits
 * write requests
 * writes

18. **InnoDB OS Log Pending Operations** in operations
 * fsyncs
 * writes

19. **InnoDB OS Log Operations** in operations/s
 * fsyncs

20. **InnoDB OS Log Bandwidth** in KiB/s
 * write

21. **InnoDB Current Row Locks** in operations
 * current waits

22. **InnoDB Row Operations** in operations/s
 * inserted
 * read
 * updated
 * deleted

23. **InnoDB Buffer Pool Pagess** in pages
 * data
 * dirty
 * free
 * misc
 * total

24. **InnoDB Buffer Pool Flush Pages Requests** in requests/s
 * flush pages

25. **InnoDB Buffer Pool Bytes** in MiB
 * data
 * dirty

26. **InnoDB Buffer Pool Operations** in operations/s
 * disk reads
 * wait free

27. **QCache Operations** in queries/s
 * hits
 * lowmem prunes
 * inserts
 * no caches

28. **QCache Queries in Cache** in queries
 * queries

29. **QCache Free Memory** in MiB
 * free

30. **QCache Memory Blocks** in blocks
 * free
 * total

31. **MyISAM Key Cache Blocks** in blocks
 * unused
 * used
 * not flushed

32. **MyISAM Key Cache Requests** in requests/s
 * reads
 * writes

33. **MyISAM Key Cache Requests** in requests/s
 * reads
 * writes

34. **MyISAM Key Cache Disk Operations** in operations/s
 * reads
 * writes

35. **Open Files** in files
 * files

36. **Opened Files Rate** in files/s
 * files

37. **Binlog Statement Cache** in statements/s
 * disk
 * all

38. **Connection Errors** in errors/s
 * accept
 * internal
 * max
 * peer addr
 * select
 * tcpwrap

39. **Slave Behind Seconds** in seconds
 * time

40. **I/O / SQL Thread Running State** in bool
 * sql
 * io

41. **Replicated Writesets** in writesets/s
 * rx
 * tx

42. **Replicated Bytes** in KiB/s
 * rx
 * tx

43. **Galera Queue** in writesets
 * rx
 * tx

44. **Replication Conflicts** in transactions
 * bf aborts
 * cert fails

45. **Flow Control** in ms
 * paused

46. **Users CPU time** in percentage
 * users

**Per user statistics:**

1. **Rows Operations** in operations/s
 * read
 * send
 * updated
 * inserted
 * deleted

2. **Commands** in commands/s
 * select
 * update
 * other


### configuration

You can provide, per server, the following:

1. username which have access to database (defaults to 'root')
2. password (defaults to none)
3. mysql my.cnf configuration file
4. mysql socket (optional)
5. mysql host (ip or hostname)
6. mysql port (defaults to 3306)
7. ssl connection parameters
 - key: the path name of the client private key file.
 - cert: the path name of the client public key certificate file.
 - ca: the path name of the Certificate Authority (CA) certificate file. This option, if used, must specify the same certificate used by the server.
 - capath: the path name of the directory that contains trusted SSL CA certificate files.
 - cipher: the list of permitted ciphers for SSL encryption.

Here is an example for 3 servers:

```yaml
update_every : 10
priority     : 90100

local:
  'my.cnf'   : '/etc/mysql/my.cnf'
  priority   : 90000

local_2:
  user     : 'root'
  pass : 'blablablabla'
  socket   : '/var/run/mysqld/mysqld.sock'
  update_every : 1

remote:
  user     : 'admin'
  pass : 'bla'
  host     : 'example.org'
  port     : 9000
```

If no configuration is given, module will attempt to connect to mysql server via unix socket at `/var/run/mysqld/mysqld.sock` without password and with username `root`

`userstats` graph works only if you enable such plugin in MariaDB server and set proper mysql priviliges (SUPER or PROCESS). For more detail please check [MariaDB User Statistics page](https://mariadb.com/kb/en/library/user-statistics/)

---

[![analytics](https://www.google-analytics.com/collect?v=1&aip=1&t=pageview&_s=1&ds=github&dr=https%3A%2F%2Fgithub.com%2Fnetdata%2Fnetdata&dl=https%3A%2F%2Fmy-netdata.io%2Fgithub%2Fcollectors%2Fpython.d.plugin%2Fmysql%2FREADME&_u=MAC~&cid=5792dfd7-8dc4-476b-af31-da2fdb9f93d2&tid=UA-64295674-3)]()
