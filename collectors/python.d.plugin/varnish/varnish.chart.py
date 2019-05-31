# -*- coding: utf-8 -*-
# Description:  varnish netdata python.d module
# Author: ilyam8
# SPDX-License-Identifier: GPL-3.0-or-later

import re

from bases.FrameworkServices.ExecutableService import ExecutableService
from bases.collection import find_binary

ORDER = [
    'session_connections',
    'client_requests',
    'all_time_hit_rate',
    'current_poll_hit_rate',
    'cached_objects_expired',
    'cached_objects_nuked',
    'threads_total',
    'threads_statistics',
    'threads_queue_len',
    'backend_connections',
    'backend_requests',
    'esi_statistics',
    'memory_usage',
    'uptime'
]

CHARTS = {
    'session_connections': {
        'options': [None, 'Connections Statistics', 'connections/s',
                    'client metrics', 'varnish.session_connection', 'line'],
        'lines': [
            ['sess_conn', 'accepted', 'incremental'],
            ['sess_dropped', 'dropped', 'incremental']
        ]
    },
    'client_requests': {
        'options': [None, 'Client Requests', 'requests/s',
                    'client metrics', 'varnish.client_requests', 'line'],
        'lines': [
            ['client_req', 'received', 'incremental']
        ]
    },
    'all_time_hit_rate': {
        'options': [None, 'All History Hit Rate Ratio', 'percentage', 'cache performance',
                    'varnish.all_time_hit_rate', 'stacked'],
        'lines': [
            ['cache_hit', 'hit', 'percentage-of-absolute-row'],
            ['cache_miss', 'miss', 'percentage-of-absolute-row'],
            ['cache_hitpass', 'hitpass', 'percentage-of-absolute-row']]
    },
    'current_poll_hit_rate': {
        'options': [None, 'Current Poll Hit Rate Ratio', 'percentage', 'cache performance',
                    'varnish.current_poll_hit_rate', 'stacked'],
        'lines': [
            ['cache_hit', 'hit', 'percentage-of-incremental-row'],
            ['cache_miss', 'miss', 'percentage-of-incremental-row'],
            ['cache_hitpass', 'hitpass', 'percentage-of-incremental-row']
        ]
    },
    'cached_objects_expired': {
        'options': [None, 'Expired Objects', 'expired/s', 'cache performance',
                    'varnish.cached_objects_expired', 'line'],
        'lines': [
            ['n_expired', 'objects', 'incremental']
        ]
    },
    'cached_objects_nuked': {
        'options': [None, 'Least Recently Used Nuked Objects', 'nuked/s', 'cache performance',
                    'varnish.cached_objects_nuked', 'line'],
        'lines': [
            ['n_lru_nuked', 'objects', 'incremental']
        ]
    },
    'threads_total': {
        'options': [None, 'Number Of Threads In All Pools', 'number', 'thread related metrics',
                    'varnish.threads_total', 'line'],
        'lines': [
            ['threads', None, 'absolute']
        ]
    },
    'threads_statistics': {
        'options': [None, 'Threads Statistics', 'threads/s', 'thread related metrics',
                    'varnish.threads_statistics', 'line'],
        'lines': [
            ['threads_created', 'created', 'incremental'],
            ['threads_failed', 'failed', 'incremental'],
            ['threads_limited', 'limited', 'incremental']
        ]
    },
    'threads_queue_len': {
        'options': [None, 'Current Queue Length', 'requests', 'thread related metrics',
                    'varnish.threads_queue_len', 'line'],
        'lines': [
            ['thread_queue_len', 'in queue']
        ]
    },
    'backend_connections': {
        'options': [None, 'Backend Connections Statistics', 'connections/s', 'backend metrics',
                    'varnish.backend_connections', 'line'],
        'lines': [
            ['backend_conn', 'successful', 'incremental'],
            ['backend_unhealthy', 'unhealthy', 'incremental'],
            ['backend_reuse', 'reused', 'incremental'],
            ['backend_toolate', 'closed', 'incremental'],
            ['backend_recycle', 'resycled', 'incremental'],
            ['backend_fail', 'failed', 'incremental']
        ]
    },
    'backend_requests': {
        'options': [None, 'Requests To The Backend', 'requests/s', 'backend metrics',
                    'varnish.backend_requests', 'line'],
        'lines': [
            ['backend_req', 'sent', 'incremental']
        ]
    },
    'esi_statistics': {
        'options': [None, 'ESI Statistics', 'problems/s', 'esi related metrics', 'varnish.esi_statistics', 'line'],
        'lines': [
            ['esi_errors', 'errors', 'incremental'],
            ['esi_warnings', 'warnings', 'incremental']
        ]
    },
    'memory_usage': {
        'options': [None, 'Memory Usage', 'MiB', 'memory usage', 'varnish.memory_usage', 'stacked'],
        'lines': [
            ['memory_free', 'free', 'absolute', 1, 1 << 20],
            ['memory_allocated', 'allocated', 'absolute', 1, 1 << 20]]
    },
    'uptime': {
        'lines': [
            ['uptime', None, 'absolute']
        ],
        'options': [None, 'Uptime', 'seconds', 'uptime', 'varnish.uptime', 'line']
    }
}

VARNISHSTAT = 'varnishstat'

re_version = re.compile(r'varnish-(?P<major>\d+)\.(?P<minor>\d+)\.(?P<patch>\d+)')


class VarnishVersion:
    def __init__(self, major, minor, patch):
        self.major = major
        self.minor = minor
        self.patch = patch

    def __str__(self):
        return '{0}.{1}.{2}'.format(self.major, self.minor, self.patch)


class Parser:
    _backend_new = re.compile(r'VBE.([\d\w_.]+)\(.*?\).(beresp[\w_]+)\s+(\d+)')
    _backend_old = re.compile(r'VBE\.[\d\w-]+\.([\w\d_]+).(beresp[\w_]+)\s+(\d+)')
    _default = re.compile(r'([A-Z]+\.)?([\d\w_.]+)\s+(\d+)')

    def __init__(self):
        self.re_default = None
        self.re_backend = None

    def init(self, data):
        data = ''.join(data)
        parsed_main = Parser._default.findall(data)
        if parsed_main:
            self.re_default = Parser._default

        parsed_backend = Parser._backend_new.findall(data)
        if parsed_backend:
            self.re_backend = Parser._backend_new
        else:
            parsed_backend = Parser._backend_old.findall(data)
            if parsed_backend:
                self.re_backend = Parser._backend_old

    def server_stats(self, data):
        return self.re_default.findall(''.join(data))

    def backend_stats(self, data):
        return self.re_backend.findall(''.join(data))


class Service(ExecutableService):
    def __init__(self, configuration=None, name=None):
        ExecutableService.__init__(self, configuration=configuration, name=name)
        self.order = ORDER
        self.definitions = CHARTS
        self.instance_name = configuration.get('instance_name')
        self.parser = Parser()
        self.command = None

    def create_command(self):
        varnishstat = find_binary(VARNISHSTAT)

        if not varnishstat:
            self.error("can't locate '{0}' binary or binary is not executable by user netdata".format(VARNISHSTAT))
            return False

        command = [varnishstat, '-V']
        reply = self._get_raw_data(stderr=True, command=command)
        if not reply:
            self.error(
                "no output from '{0}'. Is varnish running? Not enough privileges?".format(' '.join(self.command)))
            return False

        ver = parse_varnish_version(reply)
        if not ver:
            self.error("failed to parse reply from '{0}', used regex :'{1}', reply : {2}".format(
                ' '.join(command),
                re_version.pattern,
                reply,
            ))
            return False

        if self.instance_name:
            self.command = [varnishstat, '-1', '-n', self.instance_name]
        else:
            self.command = [varnishstat, '-1']

        if ver.major > 4:
            self.command.extend(['-t', '1'])

        self.info("varnish version: {0}, will use command: '{1}'".format(ver, ' '.join(self.command)))

        return True

    def check(self):
        if not self.create_command():
            return False

        # STDOUT is not empty
        reply = self._get_raw_data()
        if not reply:
            self.error("no output from '{0}'. Is it running? Not enough privileges?".format(' '.join(self.command)))
            return False

        self.parser.init(reply)

        # Output is parsable
        if not self.parser.re_default:
            self.error('cant parse the output...')
            return False

        if self.parser.re_backend:
            backends = [b[0] for b in self.parser.backend_stats(reply)[::2]]
            self.create_backends_charts(backends)
        return True

    def get_data(self):
        """
        Format data received from shell command
        :return: dict
        """
        raw = self._get_raw_data()
        if not raw:
            return None

        data = dict()
        server_stats = self.parser.server_stats(raw)
        if not server_stats:
            return None

        if self.parser.re_backend:
            backend_stats = self.parser.backend_stats(raw)
            data.update(dict(('_'.join([name, param]), value) for name, param, value in backend_stats))

        data.update(dict((param, value) for _, param, value in server_stats))

        # varnish 5 uses default.g_bytes and default.g_space
        data['memory_allocated'] = data.get('s0.g_bytes') or data.get('default.g_bytes')
        data['memory_free'] = data.get('s0.g_space') or data.get('default.g_space')

        return data

    def create_backends_charts(self, backends):
        for backend in backends:
            chart_name = ''.join([backend, '_response_statistics'])
            title = 'Backend "{0}"'.format(backend.capitalize())
            hdr_bytes = ''.join([backend, '_beresp_hdrbytes'])
            body_bytes = ''.join([backend, '_beresp_bodybytes'])

            chart = {
                chart_name:
                {
                    'options': [None, title, 'kilobits/s', 'backend response statistics',
                                'varnish.backend', 'area'],
                    'lines': [
                        [hdr_bytes, 'header', 'incremental', 8, 1000],
                        [body_bytes, 'body', 'incremental', -8, 1000]
                        ]
                    }
                }

            self.order.insert(0, chart_name)
            self.definitions.update(chart)


def parse_varnish_version(lines):
    m = re_version.search(lines[0])
    if not m:
        return None

    m = m.groupdict()
    return VarnishVersion(
        int(m['major']),
        int(m['minor']),
        int(m['patch']),
    )
