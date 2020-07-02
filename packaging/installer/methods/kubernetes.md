<!--
title: "Install Netdata on a Kubernetes cluster"
description: "Use Netdata's Helm chart to bootstrap a Netdata monitoring and troubleshooting toolkit on your Kubernetes (k8s) cluster."
custom_edit_url: https://github.com/netdata/netdata/edit/master/packaging/installer/methods/kubernetes.md
-->

# Install Netdata on a Kubernetes cluster

This document details how to install Netdata on an existing Kubernetes (k8s) cluster. By following these directions, you
will use Netdata's [Helm chart](https://github.com/netdata/helmchart) to bootstrap a Netdata deployment on your cluster.
The Helm chart installs one parent pod for storing metrics and managing alarm notifications plus an additional child pod
for every node in the cluster.

Each child pod will collect metrics from the node it runs on in addition to [22 supported
services](https://github.com/netdata/helmchart#service-discovery-and-supported-services) via [service
discovery](https://github.com/netdata/agent-service-discovery/). Each child pod will also collect
[cgroups](/collectors/cgroups.plugin/README.md),
[Kubelet](https://learn.netdata.cloud/docs/agent/collectors/go.d.plugin/modules/k8s_kubelet), and
[kube-proxy](https://learn.netdata.cloud/docs/agent/collectors/go.d.plugin/modules/k8s_kubeproxy) metrics from its node.

To install Netdata on a Kubernetes cluster, you need:

-   A working cluster running Kubernetes v1.9 or newer.
-   The [kubectl](https://kubernetes.io/docs/reference/kubectl/overview/) command line tool, within [one minor version
    difference](https://kubernetes.io/docs/tasks/tools/install-kubectl/#before-you-begin) of your cluster, on an
    administrative system.
-   The [Helm package manager](https://helm.sh/) v3.0.0 or newer on the same administrative system.

The default configuration creates one `parent` pod, installed on one of your cluster's nodes, and a DaemonSet for
additional `child` pods. This DaemonSet ensures that every node in your k8s cluster also runs a `child` pod, including
the node that also runs `parent`. The `child` pods collect metrics and stream the information to the `parent` pod, which
uses two persistent volumes to store metrics and alarms. The `parent` pod also handles alarm notifications and enables
the Netdata dashboard using an ingress controller.

## Install the Netdata Helm chart

Download the [Netdata Helm chart](https://github.com/netdata/helmchart) on the administative system where you have the
`helm` binary installed.

```bash
git clone https://github.com/netdata/helmchart.git netdata-helmchart
```

> You may not need to configure the Helm chart to get a functioning service on your cluster, but you should read the
> sections on [configuring the Helm chart](#configure-the-netdata-helm-chart) and [configuring service
> discovery](#configure-service-discovery) for details.

Install the Helm chart to your cluster with `helm install`:

```bash
helm install netdata ./netdata-helmchart
```

Run `kubectl get services` and `kubectl get pods` to confirm that your cluster now runs a `netdata` service, one
`parent` pod, and three `child` pods.

You've now installed Netdata on your Kubernetes cluster. See how to [access the Netdata
dashboard](#access-the-netdata-dashboard) to confirm it's working as expected, or see the next section to [configure the
Helm chart](#configure-the-netdata-helm-chart) to suit your cluster's particular setup.

## Configure the Netdata Helm chart

Read up on the various configuration options in the [Helm chart
documentation](https://github.com/netdata/helmchart#configuration) to see if you need to change any of the options based
on your cluster's setup.

To change a setting, use the `--set` or `--values` arguments along with `helm install`:

```bash
helm install --set a.b.c=xyz netdata ./netdata-helmchart
```

For example, to change the size of the persistent metrics volume, you would run the following:

```bash
helm install --set parent.database.volumesize=4Gi ./netdata-helmchart
```

### Configure service discovery

As mentioned in the introduction, Netdata has a [service discovery
plugin](https://github.com/netdata/agent-service-discovery/#service-discovery) to identify compatible pods and collect
metrics from the service they run. The Netdata Helm chart installs this service discovery plugin into your k8s cluster.

Service discovery scans your cluster for pods exposed on certain ports and with certain image names. By default, it
looks for its supported services on the ports they most commonly listen on, and using default image names. Service
discovery currently supports [22 popular
services](https://github.com/netdata/helmchart#service-discovery-and-supported-services).

If you haven't changed listening ports or other defaults, service discovery should find your pods, create the proper
configurations based on the service that pod runs, and begin monitoring them immediately after depolyment.

However, if you have changed some of these defaults, you'll need to copy the `netdata-helmchart/sdconfig/child.yml`
file, edit it, and pass the changed file to `helm install`/`helm upgrade`. 

First, copy the file to a new location outside the `netdata-helmchart` directory. The destination can be anywhere you
like, but the following examples assume it resides next to the `netdata-helmchart` directory.

```bash
cp netdata-helmchart/sdconfig/child.yml .
```

Edit the new `child.yml` file according to your needs. See the [Helm chart
configuration](https://github.com/netdata/helmchart#configuration) and the file itself for details. You can then run
`helm install`/`helm upgrade` with the `--set-file` argument to use your configured `child.yml` file instead of the
default, changing the path if you copied it elsewhere.

```bash
helm install --set-file sd.child.configmap.from.value=./child.yml netdata ./netdata-helmchart
helm upgrade --set-file sd.child.configmap.from.value=./child.yml netdata ./netdata-helmchart
```

Your configured service discovery is now pushed to your cluster.

## Access the Netdata dashboard

Accessing the Netdata dashboard itself depends on how you set up your k8s cluster and the Netdata Helm chart. If you
installed the Helm chart with the default `service.type=ClusterIP`, you will need to forward a port to the parent pod.

```bash
kubectl port-forward netdata-parent-0 19999:19999 
```

You can now access the dashboard at `http://CLUSTER:19999`, replacing `CLUSTER` with the IP address or hostname of your
k8s cluster.

If you set up the Netdata Helm chart with `service.type=LoadBalancer`, you can find the external IP for the load
balancer with `kubectl get services`, under the `EXTERNAL-IP` column.

```bash
kubectl get services
NAME                 TYPE           CLUSTER-IP       EXTERNAL-IP    PORT(S)              AGE
cockroachdb          ClusterIP      None             <none>         26257/TCP,8080/TCP   46h
cockroachdb-public   ClusterIP      10.245.148.233   <none>         26257/TCP,8080/TCP   46h
kubernetes           ClusterIP      10.245.0.1       <none>         443/TCP              47h
netdata              LoadBalancer   10.245.160.131   203.0.113.0    19999:32231/TCP      74m
```

In the above example, access the dashboard by navigating to `http://203.0.113.0:19999`.

## Update/reinstall the Netdata Helm chart

If you update the Helm chart's configuration, run `helm upgrade` to redeploy your Netdata service, replacing `netdata` 
with the name of the release if you changed it upon installtion:

```bash
helm upgrade netdata ./netdata-helmchart
```

## What's next?

Check out our [Agent's getting started guide](/docs/getting-started.md) for a quick overview of Netdata's capabilities,
especially if you want to change any of the configuration settings for either the parent or child nodes.

To futher configure Netdata for your cluster, see our [Helm chart repository](https://github.com/netdata/helmchart) and
the [service discovery repository](https://github.com/netdata/agent-service-discovery/).

[![analytics](https://www.google-analytics.com/collect?v=1&aip=1&t=pageview&_s=1&ds=github&dr=https%3A%2F%2Fgithub.com%2Fnetdata%2Fnetdata&dl=https%3A%2F%2Fmy-netdata.io%2Fgithub%2Finstaller%2Fmethods%2Fkubernetes&_u=MAC~&cid=5792dfd7-8dc4-476b-af31-da2fdb9f93d2&tid=UA-64295674-3)](<>)
