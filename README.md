SockIMUX
========
Inverse multiplexer using UNIX sockets.

What It Does
------------
Essentially, ``sockimux`` inverse multiplexes STDIO over multiple UNIX
sockets. A ``sockimux`` instance gets a single bidirectional stream
(STDIO) as input, and forwards it to another ``sockimux`` instance over
one or more sockets; the other ``sockimux`` reassembles the multiple
streams into a single one, and forwards it (to STDIO).

```
 STDIO            UNIX sockets            STDIO
               _                _
              / |              | \
             /  |              |  \  
            /   |<============>|   \
  STDIN ==>|    |<============>|    |<== STDIN
           |imux|      .       |imux|
 STDOUT <==|    |      .       |    |==> STDOUT
            \   |      .       |   /
             \  |<============>|  /
              \_|              |_/
```

``sockimux`` does not connect itself to anoter ``sockimux`` instance. It
is up to you to connect them, for example, with ``socat``.

Installing
----------
```bash
$ make
```

and then run tests:
```bash
$ make test
```

Usage
-----
```bash
$ ./sockimux UNIX_SOCKET
```
Where ``UNIX_SOCKET`` is the path to the UNIX socket to listen on.

Examples
--------
### Connect two sockets with one stream
```bash
$ ./sockimux /tmp/sockA > /tmp/out.txt &
$ echo hello world | ./sockimux /tmp/sockB &              
$ socat UNIX-CONNECT:/tmp/sockA UNIX-CONNECT:/tmp/sockB &
$ cat /tmp/out.txt
hello world
```

### One SSH session over multiple TCP sessions
Let's say you have an unreliable connection where individual TCP sessions
independantly suffer outages or low bandwidth. This connects to a remote
host with SSH, over multiple inverse multiplexed SSH connections. This
assumes that you have copied your public key onto the other host so that
it does not prompt for a password.

In the first terminal:
```bash
$ ssh -o "ProxyCommand /path/to/sockimux /tmp/sockA" username@
```
In another terminal:
```bash
$ ssh somehost "socat EXEC:'/path/to/sockimux /tmp/sockB' TCP:127.0.0.1:22" &
$ socat UNIX-CONNECT:/tmp/sockA EXEC:"ssh somehost socat STDIO UNIX-CONNECT\:/tmp/sockB" &
$ socat UNIX-CONNECT:/tmp/sockA EXEC:"ssh somehost socat STDIO UNIX-CONNECT\:/tmp/sockB" &
$ socat UNIX-CONNECT:/tmp/sockA EXEC:"ssh somehost socat STDIO UNIX-CONNECT\:/tmp/sockB" &
$ socat UNIX-CONNECT:/tmp/sockA EXEC:"ssh somehost socat STDIO UNIX-CONNECT\:/tmp/sockB" &
```
In the first terminal, you are now logged in over 4 inverse multiplexed
SSH sessions. This is probably more useful for copying files, where
latency and packet arrival ordering is less important.

License
-------
MIT License. See LICENSE file.
