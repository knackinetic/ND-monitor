# -*- coding: utf-8 -*-
# Description: elastic search node stats netdata python.d module
# Author: l2isbad

from base import UrlService
from socket import gethostbyname, gaierror
try:
        from queue import Queue
except ImportError:
        from Queue import Queue
from threading import Thread
from collections import namedtuple
from json import loads

# default module values (can be overridden per job in `config`)
# update_every = 2
update_every = 5
priority = 60000
retries = 60

METHODS = namedtuple('METHODS', ['get_data_function', 'url'])

NODE_STATS = [
    ('indices.search.fetch_current', None, None),
    ('indices.search.fetch_total', None, None),
    ('indices.search.query_current', None, None),
    ('indices.search.query_total', None, None),
    ('indices.search.query_time_in_millis', None, None),
    ('indices.search.fetch_time_in_millis', None, None),
    ('indices.indexing.index_total', 'indexing_index_total', None),
    ('indices.indexing.index_current', 'indexing_index_current', None),
    ('indices.indexing.index_time_in_millis', 'indexing_index_time_in_millis', None),
    ('indices.refresh.total', 'refresh_total', None),
    ('indices.refresh.total_time_in_millis', 'refresh_total_time_in_millis', None),
    ('indices.flush.total', 'flush_total', None),
    ('indices.flush.total_time_in_millis', 'flush_total_time_in_millis', None),
    ('jvm.gc.collectors.young.collection_count', 'young_collection_count', None),
    ('jvm.gc.collectors.old.collection_count', 'old_collection_count', None),
    ('jvm.gc.collectors.young.collection_time_in_millis', 'young_collection_time_in_millis', None),
    ('jvm.gc.collectors.old.collection_time_in_millis', 'old_collection_time_in_millis', None),
    ('jvm.mem.heap_used_percent', 'jvm_heap_percent', None),
    ('jvm.mem.heap_committed_in_bytes', 'jvm_heap_commit', None),
    ('thread_pool.bulk.queue', 'bulk_queue', None),
    ('thread_pool.bulk.rejected', 'bulk_rejected', None),
    ('thread_pool.index.queue', 'index_queue', None),
    ('thread_pool.index.rejected', 'index_rejected', None),
    ('thread_pool.search.queue', 'search_queue', None),
    ('thread_pool.search.rejected', 'search_rejected', None),
    ('thread_pool.merge.queue', 'merge_queue', None),
    ('thread_pool.merge.rejected', 'merge_rejected', None),
    ('indices.fielddata.memory_size_in_bytes', 'index_fdata_memory', None),
    ('indices.fielddata.evictions', None, None),
    ('breakers.fielddata.tripped', None, None),
    ('http.current_open', 'http_current_open', None),
    ('transport.rx_size_in_bytes', 'transport_rx_size_in_bytes', None),
    ('transport.tx_size_in_bytes', 'transport_tx_size_in_bytes', None),
    ('process.max_file_descriptors', None, None),
    ('process.open_file_descriptors', None, None)
]

CLUSTER_STATS = [
    ('nodes.count.data_only', 'count_data_only', None),
    ('nodes.count.master_data', 'count_master_data', None),
    ('nodes.count.total', 'count_total', None),
    ('nodes.count.master_only', 'count_master_only', None),
    ('nodes.count.client', 'count_client', None),
    ('indices.docs.count', 'docs_count', None),
    ('indices.query_cache.hit_count', 'query_cache_hit_count', None),
    ('indices.query_cache.miss_count', 'query_cache_miss_count', None),
    ('indices.store.size_in_bytes', 'store_size_in_bytes', None),
    ('indices.count', 'indices_count', None),
    ('indices.shards.total', 'shards_total', None)
]

HEALTH_STATS = [
    ('number_of_nodes', 'health_number_of_nodes', None),
    ('number_of_data_nodes', 'health_number_of_data_nodes', None),
    ('number_of_pending_tasks', 'health_number_of_pending_tasks', None),
    ('number_of_in_flight_fetch', 'health_number_of_in_flight_fetch', None),
    ('active_shards', 'health_active_shards', None),
    ('relocating_shards', 'health_relocating_shards', None),
    ('unassigned_shards', 'health_unassigned_shards', None),
    ('delayed_unassigned_shards', 'health_delayed_unassigned_shards', None),
    ('initializing_shards', 'health_initializing_shards', None),
    ('active_shards_percent_as_number', 'health_active_shards_percent_as_number', None)
]

# charts order (can be overridden if you want less charts, or different order)
ORDER = ['search_perf_total', 'search_perf_current', 'search_perf_time', 'search_latency', 'index_perf_total',
         'index_perf_current', 'index_perf_time', 'index_latency', 'jvm_mem_heap', 'jvm_gc_count',
         'jvm_gc_time', 'host_metrics_file_descriptors', 'host_metrics_http', 'host_metrics_transport',
         'thread_pool_qr_q', 'thread_pool_qr_r', 'fdata_cache', 'fdata_ev_tr', 'cluster_health_status',
         'cluster_health_nodes', 'cluster_health_shards', 'cluster_stats_nodes', 'cluster_stats_query_cache',
         'cluster_stats_docs', 'cluster_stats_store', 'cluster_stats_indices_shards']

CHARTS = {
    'search_perf_total': {
        'options': [None, 'Total number of queries, fetches', 'number of', 'search performance',
                    'es.search_query_total', 'stacked'],
        'lines': [
            ['query_total', 'queries', 'incremental'],
            ['fetch_total', 'fetches', 'incremental']
        ]},
    'search_perf_current': {
        'options': [None, 'Number of queries, fetches in progress', 'number of', 'search performance',
                    'es.search_query_current', 'stacked'],
        'lines': [
            ['query_current', 'queries', 'absolute'],
            ['fetch_current', 'fetches', 'absolute']
        ]},
    'search_perf_time': {
        'options': [None, 'Time spent on queries, fetches', 'seconds', 'search performance',
                    'es.search_time', 'stacked'],
        'lines': [
            ['query_time_in_millis', 'query', 'incremental', 1, 1000],
            ['fetch_time_in_millis', 'fetch', 'incremental', 1, 1000]
        ]},
    'search_latency': {
        'options': [None, 'Query and fetch latency', 'ms', 'search performance', 'es.search_latency', 'stacked'],
        'lines': [
            ['query_latency', 'query', 'absolute', 1, 1000],
            ['fetch_latency', 'fetch', 'absolute', 1, 1000]
        ]},
    'index_perf_total': {
        'options': [None, 'Total number of documents indexed, index refreshes, index flushes to disk', 'number of',
                    'indexing performance', 'es.index_performance_total', 'stacked'],
        'lines': [
            ['indexing_index_total', 'indexed', 'incremental'],
            ['refresh_total', 'refreshes', 'incremental'],
            ['flush_total', 'flushes', 'incremental']
        ]},
    'index_perf_current': {
        'options': [None, 'Number of documents currently being indexed', 'currently indexed',
                    'indexing performance', 'es.index_performance_current', 'stacked'],
        'lines': [
            ['indexing_index_current', 'documents', 'absolute']
        ]},
    'index_perf_time': {
        'options': [None, 'Time spent on indexing, refreshing, flushing', 'seconds', 'indexing performance',
                    'es.search_time', 'stacked'],
        'lines': [
            ['indexing_index_time_in_millis', 'indexing', 'incremental', 1, 1000],
            ['refresh_total_time_in_millis', 'refreshing', 'incremental', 1, 1000],
            ['flush_total_time_in_millis', 'flushing', 'incremental', 1, 1000]
        ]},
    'index_latency': {
        'options': [None, 'Indexing and flushing latency', 'ms', 'indexing performance',
                    'es.index_latency', 'stacked'],
        'lines': [
            ['indexing_latency', 'indexing', 'absolute', 1, 1000],
            ['flushing_latency', 'flushing', 'absolute', 1, 1000]
        ]},
    'jvm_mem_heap': {
        'options': [None, 'JVM heap currently in use/committed', 'percent/MB', 'memory usage and gc',
                    'es.jvm_heap', 'area'],
        'lines': [
            ['jvm_heap_percent', 'inuse', 'absolute'],
            ['jvm_heap_commit', 'commit', 'absolute', -1, 1048576]
        ]},
    'jvm_gc_count': {
        'options': [None, 'Count of garbage collections', 'counts', 'memory usage and gc', 'es.gc_count', 'stacked'],
        'lines': [
            ['young_collection_count', 'young', 'incremental'],
            ['old_collection_count', 'old', 'incremental']
        ]},
    'jvm_gc_time': {
        'options': [None, 'Time spent on garbage collections', 'ms', 'memory usage and gc', 'es.gc_time', 'stacked'],
        'lines': [
            ['young_collection_time_in_millis', 'young', 'incremental'],
            ['old_collection_time_in_millis', 'old', 'incremental']
        ]},
    'thread_pool_qr_q': {
        'options': [None, 'Number of queued threads in thread pool', 'queued threads', 'queues and rejections',
                    'es.thread_pool_queued', 'stacked'],
        'lines': [
            ['bulk_queue', 'bulk', 'absolute'],
            ['index_queue', 'index', 'absolute'],
            ['search_queue', 'search', 'absolute'],
            ['merge_queue', 'merge', 'absolute']
        ]},
    'thread_pool_qr_r': {
        'options': [None, 'Number of rejected threads in thread pool', 'rejected threads', 'queues and rejections',
                    'es.thread_pool_rejected', 'stacked'],
        'lines': [
            ['bulk_rejected', 'bulk', 'absolute'],
            ['index_rejected', 'index', 'absolute'],
            ['search_rejected', 'search', 'absolute'],
            ['merge_rejected', 'merge', 'absolute']
        ]},
    'fdata_cache': {
        'options': [None, 'Fielddata cache size', 'MB', 'fielddata cache', 'es.fdata_cache', 'line'],
        'lines': [
            ['index_fdata_memory', 'cache', 'absolute', 1, 1048576]
        ]},
    'fdata_ev_tr': {
        'options': [None, 'Fielddata evictions and circuit breaker tripped count', 'number of events',
                    'fielddata cache', 'es.evictions_tripped', 'line'],
        'lines': [
            ['evictions', None, 'incremental'],
            ['tripped', None, 'incremental']
        ]},
    'cluster_health_nodes': {
        'options': [None, 'Nodes and tasks statistics', 'units', 'cluster health API',
                    'es.cluster_health_nodes', 'stacked'],
        'lines': [
            ['health_number_of_nodes', 'nodes', 'absolute'],
            ['health_number_of_data_nodes', 'data_nodes', 'absolute'],
            ['health_number_of_pending_tasks', 'pending_tasks', 'absolute'],
            ['health_number_of_in_flight_fetch', 'in_flight_fetch', 'absolute']
        ]},
    'cluster_health_status': {
        'options': [None, 'Cluster status', 'status', 'cluster health API',
                    'es.cluster_health_status', 'area'],
        'lines': [
            ['status_green', 'green', 'absolute'],
            ['status_red', 'red', 'absolute'],
            ['status_foo1', None, 'absolute'],
            ['status_foo2', None, 'absolute'],
            ['status_foo3', None, 'absolute'],
            ['status_yellow', 'yellow', 'absolute']
        ]},
    'cluster_health_shards': {
        'options': [None, 'Shards statistics', 'shards', 'cluster health API',
                    'es.cluster_health_shards', 'stacked'],
        'lines': [
            ['health_active_shards', 'active_shards', 'absolute'],
            ['health_relocating_shards', 'relocating_shards', 'absolute'],
            ['health_unassigned_shards', 'unassigned', 'absolute'],
            ['health_delayed_unassigned_shards', 'delayed_unassigned', 'absolute'],
            ['health_initializing_shards', 'initializing', 'absolute'],
            ['health_active_shards_percent_as_number', 'active_percent', 'absolute']
        ]},
    'cluster_stats_nodes': {
        'options': [None, 'Nodes statistics', 'nodes', 'cluster stats API',
                    'es.cluster_nodes', 'stacked'],
        'lines': [
            ['count_data_only', 'data_only', 'absolute'],
            ['count_master_data', 'master_data', 'absolute'],
            ['count_total', 'total', 'absolute'],
            ['count_master_only', 'master_only', 'absolute'],
            ['count_client', 'client', 'absolute']
        ]},
    'cluster_stats_query_cache': {
        'options': [None, 'Query cache statistics', 'queries', 'cluster stats API',
                    'es.cluster_query_cache', 'stacked'],
        'lines': [
            ['query_cache_hit_count', 'hit', 'incremental'],
            ['query_cache_miss_count', 'miss', 'incremental']
        ]},
    'cluster_stats_docs': {
        'options': [None, 'Docs statistics', 'count', 'cluster stats API',
                    'es.cluster_docs', 'line'],
        'lines': [
            ['docs_count', 'docs', 'absolute']
        ]},
    'cluster_stats_store': {
        'options': [None, 'Store statistics', 'MB', 'cluster stats API',
                    'es.cluster_store', 'line'],
        'lines': [
            ['store_size_in_bytes', 'size', 'absolute', 1, 1048567]
        ]},
    'cluster_stats_indices_shards': {
        'options': [None, 'Indices and shards statistics', 'count', 'cluster stats API',
                    'es.cluster_indices_shards', 'stacked'],
        'lines': [
            ['indices_count', 'indices', 'absolute'],
            ['shards_total', 'shards', 'absolute']
        ]},
    'host_metrics_transport': {
        'options': [None, 'Cluster communication transport metrics', 'kbit/s', 'host metrics',
                    'es.host_transport', 'area'],
        'lines': [
            ['transport_rx_size_in_bytes', 'in', 'incremental', 8, 1000],
            ['transport_tx_size_in_bytes', 'out', 'incremental', -8, 1000]
        ]},
    'host_metrics_file_descriptors': {
        'options': [None, 'Available file descriptors in percent', 'percent', 'host metrics',
                    'es.host_descriptors', 'area'],
        'lines': [
            ['file_descriptors_used', 'used', 'absolute', 1, 10]
        ]},
    'host_metrics_http': {
        'options': [None, 'Opened HTTP connections', 'connections', 'host metrics',
                    'es.host_http_connections', 'line'],
        'lines': [
            ['http_current_open', 'opened', 'absolute', 1, 1]
        ]}
}


class Service(UrlService):
    def __init__(self, configuration=None, name=None):
        UrlService.__init__(self, configuration=configuration, name=name)
        self.order = ORDER
        self.definitions = CHARTS
        self.host = self.configuration.get('host')
        self.port = self.configuration.get('port', 9200)
        self.scheme = self.configuration.get('scheme', 'http')
        self.latency = dict()
        self.methods = list()

    def check(self):
        # We can't start if <host> AND <port> not specified
        if not all([self.host, self.port, isinstance(self.host, str), isinstance(self.port, (str, int))]):
            self.error('Host is not defined in the module configuration file')
            return False

        # It as a bad idea to use hostname.
        # Hostname -> ip address
        try:
            self.host = gethostbyname(self.host)
        except gaierror as error:
            self.error(str(error))
            return False

        scheme = 'http' if self.scheme == 'http' else 'https'
        # Add handlers (auth, self signed cert accept)
        self.url = '%s://%s:%s' % (scheme, self.host, self.port)
        self.opener = self._build_opener()
        # Create URL for every Elasticsearch API
        url_node_stats = '%s://%s:%s/_nodes/_local/stats' % (scheme, self.host, self.port)
        url_cluster_health = '%s://%s:%s/_cluster/health' % (scheme, self.host, self.port)
        url_cluster_stats = '%s://%s:%s/_cluster/stats' % (scheme, self.host, self.port)

        # Create list of enabled API calls
        user_choice = [bool(self.configuration.get('node_stats', True)),
                       bool(self.configuration.get('cluster_health', True)),
                       bool(self.configuration.get('cluster_stats', True))]

        avail_methods = [METHODS(get_data_function=self._get_node_stats_, url=url_node_stats),
                         METHODS(get_data_function=self._get_cluster_health_, url=url_cluster_health),
                         METHODS(get_data_function=self._get_cluster_stats_, url=url_cluster_stats)]

        # Remove disabled API calls from 'avail methods'
        self.methods = [avail_methods[e[0]] for e in enumerate(avail_methods) if user_choice[e[0]]]

        # Run _get_data for ALL active API calls. 
        api_check_result = dict()
        data_from_check = dict()
        for method in self.methods:
            try:
                api_check_result[method.url] = method.get_data_function(None, method.url)
                data_from_check.update(api_check_result[method.url] or dict())
            except KeyError as error:
                self.error('Failed to parse %s. Error: %s'  % (method.url, str(error)))
                return False

        # We can start ONLY if all active API calls returned NOT None
        if not all(api_check_result.values()):
            self.error('Plugin could not get data from all APIs')
            return False
        else:
            self._data_from_check = data_from_check
            return True

    def _get_data(self):
        threads = list()
        queue = Queue()
        result = dict()

        for method in self.methods:
            th = Thread(target=method.get_data_function, args=(queue, method.url))
            th.start()
            threads.append(th)

        for thread in threads:
            thread.join()
            result.update(queue.get())

        return result or None

    def _get_cluster_health_(self, queue, url):
        """
        Format data received from http request
        :return: dict
        """

        raw_data = self._get_raw_data(url)

        if not raw_data:
            return queue.put(dict()) if queue else None
        else:
            data = loads(raw_data)

            to_netdata = fetch_data_(raw_data=data, metrics_list=HEALTH_STATS)

            to_netdata.update({'status_green': 0, 'status_red': 0, 'status_yellow': 0,
                               'status_foo1': 0, 'status_foo2': 0, 'status_foo3': 0})
            current_status = 'status_' + data['status']
            to_netdata[current_status] = 1

            return queue.put(to_netdata) if queue else to_netdata

    def _get_cluster_stats_(self, queue, url):
        """
        Format data received from http request
        :return: dict
        """

        raw_data = self._get_raw_data(url)

        if not raw_data:
            return queue.put(dict()) if queue else None
        else:
            data = loads(raw_data)

            to_netdata = fetch_data_(raw_data=data, metrics_list=CLUSTER_STATS)

            return queue.put(to_netdata) if queue else to_netdata

    def _get_node_stats_(self, queue, url):
        """
        Format data received from http request
        :return: dict
        """

        raw_data = self._get_raw_data(url)

        if not raw_data:
            return queue.put(dict()) if queue else None
        else:
            data = loads(raw_data)

            node = list(data['nodes'].keys())[0]
            to_netdata = fetch_data_(raw_data=data['nodes'][node], metrics_list=NODE_STATS)

            # Search performance latency
            to_netdata['query_latency'] = self.find_avg_(to_netdata['query_total'],
                                                         to_netdata['query_time_in_millis'], 'query_latency')
            to_netdata['fetch_latency'] = self.find_avg_(to_netdata['fetch_total'],
                                                         to_netdata['fetch_time_in_millis'], 'fetch_latency')

            # Indexing performance latency
            to_netdata['indexing_latency'] = self.find_avg_(to_netdata['indexing_index_total'],
                                                            to_netdata['indexing_index_time_in_millis'], 'index_latency')
            to_netdata['flushing_latency'] = self.find_avg_(to_netdata['flush_total'],
                                                            to_netdata['flush_total_time_in_millis'], 'flush_latency')

            to_netdata['file_descriptors_used'] = round(float(to_netdata['open_file_descriptors'])
                                                        / to_netdata['max_file_descriptors'] * 1000)

            return queue.put(to_netdata) if queue else to_netdata

    def find_avg_(self, value1, value2, key):
        if key not in self.latency:
            self.latency.update({key: [value1, value2]})
            return 0
        else:
            if not self.latency[key][0] == value1:
                latency = round(float(value2 - self.latency[key][1]) / float(value1 - self.latency[key][0]) * 1000)
                self.latency.update({key: [value1, value2]})
                return latency
            else:
                self.latency.update({key: [value1, value2]})
                return 0


def fetch_data_(raw_data, metrics_list):
    to_netdata = dict()
    for metric, new_name, function in metrics_list:
        value = raw_data
        for key in metric.split('.'):
            try:
                value = value[key]
            except KeyError:
                break
        if not isinstance(value, dict) and key:
            to_netdata[new_name or key] = value if not function else function(value)

    return to_netdata

