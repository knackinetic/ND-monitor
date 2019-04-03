# Install netdata with Docker

> :warning: As of Sep 9th, 2018 we ship [new docker builds](https://github.com/netdata/netdata/pull/3995), running netdata in docker with an [ENTRYPOINT](https://docs.docker.com/engine/reference/builder/#entrypoint) directive, not a COMMAND directive. Please adapt your execution scripts accordingly. You can find more information about ENTRYPOINT vs COMMAND is presented by goinbigdata [here](http://goinbigdata.com/docker-run-vs-cmd-vs-entrypoint/) and by docker docs [here](https://docs.docker.com/engine/reference/builder/#understand-how-cmd-and-entrypoint-interact).
>
> Also, the `latest` is now based on alpine, so **`alpine` is not updated any more** and `armv7hf` is now replaced with `armhf` (to comply with https://github.com/multiarch naming), so **`armv7hf` is not updated** either.

## Limitations

Running netdata in a container for monitoring the whole host, can limit its capabilities. Some data is not accessible or not as detailed as when running netdata on the host.

## Package scrambling in runtime (x86_64 only)

By default on x86_64 architecture our docker images use Polymorphic Polyverse Linux package scrambling. For increased security you can enable rescrambling of packages during runtime. To do this set environment variable `RESCRAMBLE=true` while starting netdata docker container.

For more information go to [Polyverse site](https://polyverse.io/how-it-works/)

## Run netdata with docker command

Quickly start netdata with the docker command line.
Netdata is then available at http://host:19999

This is good for an internal network or to quickly analyse a host.

```bash
docker run -d --name=netdata \
  -p 19999:19999 \
  -v /proc:/host/proc:ro \
  -v /sys:/host/sys:ro \
  -v /var/run/docker.sock:/var/run/docker.sock:ro \
  --cap-add SYS_PTRACE \
  --security-opt apparmor=unconfined \
  netdata/netdata
```

The above can be converted to docker-compose file for ease of management:

```yaml
version: '3'
services:
  netdata:
    image: netdata/netdata
    hostname: example.com # set to fqdn of host
    ports:
      - 19999:19999
    cap_add:
      - SYS_PTRACE
    security_opt:
      - apparmor:unconfined
    volumes:
      - /proc:/host/proc:ro
      - /sys:/host/sys:ro
      - /var/run/docker.sock:/var/run/docker.sock:ro
```

### Docker container names resolution

If you want to have your container names resolved by netdata it needs to have access to docker group. To achive that just add environment variable `PGID=999` to netdata container, where `999` is a docker group id from your host. This number can be found by running:
```bash
grep docker /etc/group | cut -d ':' -f 3
```

### Pass command line options to Netdata 

Since we use an [ENTRYPOINT](https://docs.docker.com/engine/reference/builder/#entrypoint) directive, you can provide [netdata daemon command line options](https://docs.netdata.cloud/daemon/#command-line-options) such as the IP address netdata will be running on, using the [command instruction](https://docs.docker.com/engine/reference/builder/#cmd). 

## Install Netdata using Docker Compose with SSL/TLS enabled http proxy

For a permanent installation on a public server, you should [secure the netdata instance](../../docs/netdata-security.md). This section contains an example of how to install netdata with an SSL reverse proxy and basic authentication.

You can use use the following docker-compose.yml and Caddyfile files to run netdata with docker. Replace the Domains and email address for [Letsencrypt](https://letsencrypt.org/) before starting.

### Prerequisites
* [Docker](https://docs.docker.com/install/#server)
* [Docker Compose](https://docs.docker.com/compose/install/)
* Domain configured in DNS pointing to host.

### Caddyfile

This file needs to be placed in /opt with name `Caddyfile`. Here you customize your domain and you need to provide your email address to obtain a Letsencrypt certificate. Certificate renewal will happen automatically and will be executed internally by the caddy server.

```
netdata.example.org {
  proxy / netdata:19999
  tls admin@example.org
}
```

### docker-compose.yml

After setting Caddyfile run this with `docker-compose up -d` to have fully functioning netdata setup behind HTTP reverse proxy.

```yaml
version: '3'
volumes:
  caddy:

services:
  caddy:
    image: abiosoft/caddy
    ports:
      - 80:80
      - 443:443
    volumes:
      - /opt/Caddyfile:/etc/Caddyfile
      - caddy:/root/.caddy
    environment:
      ACME_AGREE: 'true'
  netdata:
    restart: always
    hostname: netdata.example.org
    image: netdata/netdata
    cap_add:
      - SYS_PTRACE
    security_opt:
      - apparmor:unconfined
    volumes:
      - /proc:/host/proc:ro
      - /sys:/host/sys:ro
      - /var/run/docker.sock:/var/run/docker.sock:ro
```

### Restrict access with basic auth

You can restrict access by following [official caddy guide](https://caddyserver.com/docs/basicauth) and adding lines to Caddyfile.

[![analytics](https://www.google-analytics.com/collect?v=1&aip=1&t=pageview&_s=1&ds=github&dr=https%3A%2F%2Fgithub.com%2Fnetdata%2Fnetdata&dl=https%3A%2F%2Fmy-netdata.io%2Fgithub%2Fpackaging%2Fdocker%2FREADME&_u=MAC~&cid=5792dfd7-8dc4-476b-af31-da2fdb9f93d2&tid=UA-64295674-3)]()

### Publish a test image to your own repository

The script `packaging/docker/build-test.sh` can be used to create an image and upload it to a repository of your choosing. 

```
Usage: packaging/docker/build-test.sh -r <REPOSITORY> -v <VERSION> -u <DOCKER_USERNAME> -p <DOCKER_PASS> [-s]
	-s skip build, just push the image
Builds an amd64 image and pushes it to the docker hub repository REPOSITORY
```

This is especially useful when testing a Pull Request for Kubernetes, since you can set `image` to an immutable repository and tag, set the `imagePullPolicy` to `Always` and just keep uploading new images.

Example:

We get a local copy of the Helm chart at https://github.com/netdata/helmchart. We modify `values.yaml` to have the following:

```
image:
  repository: cakrit/netdata-prs
  tag: PR5576
  pullPolicy: Always
```

We check out PR5576 and run the following:
```
./packaging/docker/build-test.sh -r cakrit/netdata-prs -v PR5576 -u cakrit -p 'XXX'
```

Then we can run `helm install [path to our helmchart clone]`.

If we make changes to the code, we execute the same `build-test.sh` command, followed by `helm upgrade [name] [path to our helmchart clone]`
