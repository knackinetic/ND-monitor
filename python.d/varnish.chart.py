# -*- coding: utf-8 -*-
# Description:  varnish netdata python.d module
# Author: l2isbad

from base import SimpleService
from re import compile
from os import access as is_executable, X_OK
from subprocess import Popen, PIPE


# default module values (can be overridden per job in `config`)
# update_every = 2
priority = 60000
retries = 60

ORDER = ['session', 'hit_rate', 'chit_rate', 'expunge', 'threads', 'backend_health', 'memory_usage', 'bad', 'uptime']

CHARTS = {'backend_health': 
             {'lines': [['backend_conn', 'conn', 'incremental', 1, 1],
                       ['backend_unhealthy', 'unhealthy', 'incremental', 1, 1],
                       ['backend_busy', 'busy', 'incremental', 1, 1],
                       ['backend_fail', 'fail', 'incremental', 1, 1],
                       ['backend_reuse', 'reuse', 'incremental', 1, 1],
                       ['backend_recycle', 'resycle', 'incremental', 1, 1],
                       ['backend_toolate', 'toolate', 'incremental', 1, 1],
                       ['backend_retry', 'retry', 'incremental', 1, 1],
                       ['backend_req', 'req', 'incremental', 1, 1]],
              'options': [None, 'Backend health', 'connections', 'Backend health', 'varnish.backend_traf', 'line']},
          'bad': 
             {'lines': [['sess_drop_b', None, 'incremental', 1, 1],
                       ['backend_unhealthy_b', None, 'incremental', 1, 1],
                       ['fetch_failed', None, 'incremental', 1, 1],
                       ['backend_busy_b', None, 'incremental', 1, 1],
                       ['threads_failed_b', None, 'incremental', 1, 1],
                       ['threads_limited_b', None, 'incremental', 1, 1],
                       ['threads_destroyed_b', None, 'incremental', 1, 1],
                       ['thread_queue_len_b', 'queue_len', 'absolute', 1, 1],
                       ['losthdr_b', None, 'incremental', 1, 1],
                       ['esi_errors_b', None, 'incremental', 1, 1],
                       ['esi_warnings_b', None, 'incremental', 1, 1],
                       ['sess_fail_b', None, 'incremental', 1, 1],
                       ['sc_pipe_overflow_b', None, 'incremental', 1, 1],
                       ['sess_pipe_overflow_b', None, 'incremental', 1, 1]],
              'options': [None, 'Misbehavior', 'problems', 'Problems summary', 'varnish.bad', 'line']},
          'expunge':
             {'lines': [['n_expired', 'expired', 'incremental', 1, 1],
                       ['n_lru_nuked', 'lru_nuked', 'incremental', 1, 1]],
              'options': [None, 'Object expunging', 'objects', 'Cache performance', 'varnish.expunge', 'line']},
          'hit_rate': 
             {'lines': [['cache_hit_perc', 'hit', 'absolute', 1, 100],
                       ['cache_miss_perc', 'miss', 'absolute', 1, 100],
                       ['cache_hitpass_perc', 'hitpass', 'absolute', 1, 100]],
              'options': [None, 'All history hit rate ratio','percent', 'Cache performance', 'varnish.hit_rate', 'stacked']},
          'chit_rate': 
             {'lines': [['cache_hit_cperc', 'hit', 'absolute', 1, 100],
                       ['cache_miss_cperc', 'miss', 'absolute', 1, 100],
                       ['cache_hitpass_cperc', 'hitpass', 'absolute', 1, 100]],
              'options': [None, 'Current poll hit rate ratio','percent', 'Cache performance', 'varnish.chit_rate', 'stacked']},
          'memory_usage': 
             {'lines': [['s0.g_space', 'available', 'absolute', 1, 1048576],
                       ['s0.g_bytes', 'allocated', 'absolute', -1, 1048576]],
              'options': [None, 'Memory usage', 'megabytes', 'Memory usage', 'varnish.memory_usage', 'stacked']},
          'session': 
             {'lines': [['sess_conn', 'sess_conn', 'incremental', 1, 1],
                       ['client_req', 'client_requests', 'incremental', 1, 1],
                       ['client_conn', 'client_conn', 'incremental', 1, 1],
                       ['client_drop', 'client_drop', 'incremental', 1, 1],
                       ['sess_dropped', 'sess_dropped', 'incremental', 1, 1]],
              'options': [None, 'Sessions', 'units', 'Client metrics', 'varnish.session', 'line']},
          'threads': 
             {'lines': [['threads', None, 'absolute', 1, 1],
                       ['threads_created', 'created', 'incremental', 1, 1],
                       ['threads_failed', 'failed', 'incremental', 1, 1],
                       ['threads_limited', 'limited', 'incremental', 1, 1],
                       ['thread_queue_len', 'queue_len', 'incremental', 1, 1],
                       ['sess_queued', 'sess_queued', 'incremental', 1, 1]],
              'options': [None, 'Thread status', 'threads', 'Thread-related metrics', 'varnish.threads', 'line']},
          'uptime': 
             {'lines': [['uptime', None, 'absolute', 1, 1]],
              'options': [None, 'Varnish uptime', 'seconds', 'Uptime', 'varnish.uptime', 'line']}
}

DIRECTORIES = ['/bin/', '/usr/bin/', '/sbin/', '/usr/sbin/']


class Service(SimpleService):
    def __init__(self, configuration=None, name=None):
        SimpleService.__init__(self, configuration=configuration, name=name)
        try:
            self.varnish = [''.join([directory, 'varnishstat']) for directory in DIRECTORIES
                         if is_executable(''.join([directory, 'varnishstat']), X_OK)][0]
        except IndexError:
            self.varnish = False
        self.rgx_all = compile(r'([A-Z]+\.)?([\d\w_.]+)\s+(\d+)')
        # Could be
        # VBE.boot.super_backend.pipe_hdrbyte (new)
        # or
        # VBE.default2(127.0.0.2,,81).bereq_bodybytes (old)
        # Regex result: [('super_backend', 'beresp_hdrbytes', '0'), ('super_backend', 'beresp_bodybytes', '0')]
        self.rgx_bck = (compile(r'VBE.([\d\w_.]+)\(.*?\).(beresp[\w_]+)\s+(\d+)'),
                        compile(r'VBE\.[\d\w-]+\.([\w\d_]+).(beresp[\w_]+)\s+(\d+)'))
        self.cache_prev = list()

    def check(self):
        # Cant start without 'varnishstat' command
        if not self.varnish:
            self.error('\'varnishstat\' command was not found in %s or not executable by netdata' % DIRECTORIES)
            return False

        # If command is present and we can execute it we need to make sure..
        # 1. STDOUT is not empty
        reply = self._get_raw_data()
        if not reply:
            self.error('No output from \'varnishstat\' (not enough privileges?)')
            return False

        # 2. Output is parsable (list is not empty after regex findall)
        is_parsable = self.rgx_all.findall(reply)
        if not is_parsable:
            self.error('Cant parse output...')
            return False

        # We need to find the right regex for backend parse
        self.backend_list = self.rgx_bck[0].findall(reply)[::2]
        if self.backend_list:
            self.rgx_bck = self.rgx_bck[0]
        else:
            self.backend_list = self.rgx_bck[1].findall(reply)[::2]
            self.rgx_bck = self.rgx_bck[1]

        # We are about to start!
        self.create_charts()

        self.info('Plugin was started successfully')
        return True
     
    def _get_raw_data(self):
        try:
            reply = Popen([self.varnish, '-1'], stdout=PIPE, stderr=PIPE, shell=False)
        except OSError:
            return None

        raw_data = reply.communicate()[0]

        if not raw_data:
            return None

        return raw_data

    def _get_data(self):
        """
        Format data received from shell command
        :return: dict
        """
        raw_data = self._get_raw_data()
        data_all = self.rgx_all.findall(raw_data)
        data_backend = self.rgx_bck.findall(raw_data)

        if not data_all:
            return None

        # 1. ALL data from 'varnishstat -1'. t - type(MAIN, MEMPOOL etc)
        to_netdata = {k: int(v) for t, k, v in data_all}
        
        # 2. ADD backend statistics
        to_netdata.update({'_'.join([n, k]): int(v) for n, k, v in data_backend})

        # 3. ADD additional keys to dict
        # 3.1 Cache hit/miss/hitpass OVERALL in percent
        cache_summary = sum([to_netdata.get('cache_hit', 0), to_netdata.get('cache_miss', 0),
                             to_netdata.get('cache_hitpass', 0)])
        to_netdata['cache_hit_perc'] = find_percent(to_netdata.get('cache_hit', 0), cache_summary, 10000)
        to_netdata['cache_miss_perc'] = find_percent(to_netdata.get('cache_miss', 0), cache_summary, 10000)
        to_netdata['cache_hitpass_perc'] = find_percent(to_netdata.get('cache_hitpass', 0), cache_summary, 10000)

        # 3.2 Cache hit/miss/hitpass CURRENT in percent
        if self.cache_prev:
            cache_summary = sum([to_netdata.get('cache_hit', 0), to_netdata.get('cache_miss', 0),
                                 to_netdata.get('cache_hitpass', 0)]) - sum(self.cache_prev)
            to_netdata['cache_hit_cperc'] = find_percent(to_netdata.get('cache_hit', 0) - self.cache_prev[0], cache_summary, 10000)
            to_netdata['cache_miss_cperc'] = find_percent(to_netdata.get('cache_miss', 0) - self.cache_prev[1], cache_summary, 10000)
            to_netdata['cache_hitpass_cperc'] = find_percent(to_netdata.get('cache_hitpass', 0) - self.cache_prev[2], cache_summary, 10000)
        else:
            to_netdata['cache_hit_cperc'] = 0
            to_netdata['cache_miss_cperc'] = 0
            to_netdata['cache_hitpass_cperc'] = 0

        self.cache_prev = [to_netdata.get('cache_hit', 0), to_netdata.get('cache_miss', 0), to_netdata.get('cache_hitpass', 0)]

        # 3.3 Problems summary chart
        for elem in ['backend_busy', 'backend_unhealthy', 'esi_errors', 'esi_warnings', 'losthdr', 'sess_drop', 'sc_pipe_overflow',
                     'sess_fail', 'sess_pipe_overflow', 'threads_destroyed', 'threads_failed', 'threads_limited', 'thread_queue_len']:
            if to_netdata.get(elem) is not None:
                to_netdata[''.join([elem, '_b'])] = to_netdata.get(elem)

        # Ready steady go!
        return to_netdata

    def create_charts(self):
        # If 'all_charts' is true...ALL charts are displayed. If no only default + 'extra_charts'
        #if self.configuration.get('all_charts'):
        #    self.order = EXTRA_ORDER
        #else:
        #    try:
        #        extra_charts = list(filter(lambda chart: chart in EXTRA_ORDER, self.extra_charts.split()))
        #    except (AttributeError, NameError, ValueError):
        #        self.error('Extra charts disabled.')
        #        extra_charts = []
    
        self.order = ORDER[:]
        #self.order.extend(extra_charts)

        # Create static charts
        #self.definitions = {chart: values for chart, values in CHARTS.items() if chart in self.order}
        self.definitions = CHARTS
 
        # Create dynamic backend charts
        if self.backend_list:
            for backend in self.backend_list:
                self.order.insert(0, ''.join([backend[0], '_resp_stats']))
                self.definitions.update({''.join([backend[0], '_resp_stats']): {
                    'options': [None,
                                '%s response statistics' % backend[0].capitalize(),
                                "kilobit/s",
                                'Backend response',
                                'varnish.backend',
                                'area'],
                    'lines': [[''.join([backend[0], '_beresp_hdrbytes']),
                               'header', 'incremental', 8, 1000],
                              [''.join([backend[0], '_beresp_bodybytes']),
                               'body', 'incremental', -8, 1000]]}})


def find_percent(value1, value2, multiply):
    # If value2 is 0 return 0
    if not value2:
        return 0
    else:
        return round(float(value1) / float(value2) * multiply)
