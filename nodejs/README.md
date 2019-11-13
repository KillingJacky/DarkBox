supervisor conf

```
root@pve-home:~# cat /etc/supervisor/conf.d/recomputer.conf
[program:recomputer]
command=node index.js
directory=/root/recomputer
autostart=true
autorestart=true
startsecs=5
startretries=1800
stopwaitsecs=10
stopsignal=INT
stopasgroup=true  ;this is essential
user=root
redirect_stderr=true
stdout_logfile=/root/supervisor_log/recomputer.log
stdout_logfile_maxbytes=5MB
stdout_logfile_backups=10
```