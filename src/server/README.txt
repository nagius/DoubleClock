
From https://github.com/ropg/ezTime/tree/master/server

To run it:
```
docker-compose up -d --build
```

To test it use
```
echo -n "Europe/London" | nc -u timezoned.rop.nl 2342
```

Configure fake DNS:
```
192.168.18.2    timezoned.rop.nl
```

