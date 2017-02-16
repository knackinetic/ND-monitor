# -*- coding: utf-8 -*-
# Description: web log netdata python.d module
# Author: l2isbad

from base import LogService
import re
import bisect
from os import access, R_OK
from os.path import getsize
from collections import namedtuple
from copy import deepcopy

priority = 60000
retries = 60

ORDER = ['response_statuses', 'response_codes', 'bandwidth', 'response_time', 'requests_per_url', 'http_method',
         'requests_per_ipproto', 'clients', 'clients_all']
CHARTS = {
    'response_codes': {
        'options': [None, 'Response Codes', 'requests/s', 'responses', 'web_log.response_codes', 'stacked'],
        'lines': [
            ['2xx', '2xx', 'incremental'],
            ['5xx', '5xx', 'incremental'],
            ['3xx', '3xx', 'incremental'],
            ['4xx', '4xx', 'incremental'],
            ['1xx', '1xx', 'incremental'],
            ['0xx', 'other', 'incremental'],
            ['unmatched', 'unmatched', 'incremental']
        ]},
    'bandwidth': {
        'options': [None, 'Bandwidth', 'KB/s', 'bandwidth', 'web_log.bandwidth', 'area'],
        'lines': [
            ['resp_length', 'received', 'incremental', 1, 1024],
            ['bytes_sent', 'sent', 'incremental', -1, 1024]
        ]},
    'response_time': {
        'options': [None, 'Processing Time', 'milliseconds', 'timings', 'web_log.response_time', 'area'],
        'lines': [
            ['resp_time_min', 'min', 'incremental', 1, 1000],
            ['resp_time_max', 'max', 'incremental', 1, 1000],
            ['resp_time_avg', 'avg', 'incremental', 1, 1000]
        ]},
    'clients': {
        'options': [None, 'Current Poll Unique Client IPs', 'unique ips', 'clients', 'web_log.clients', 'stacked'],
        'lines': [
            ['unique_cur_ipv4', 'ipv4', 'incremental', 1, 1],
            ['unique_cur_ipv6', 'ipv6', 'incremental', 1, 1]
        ]},
    'clients_all': {
        'options': [None, 'All Time Unique Client IPs', 'unique ips', 'clients', 'web_log.clients_all', 'stacked'],
        'lines': [
            ['unique_tot_ipv4', 'ipv4', 'absolute', 1, 1],
            ['unique_tot_ipv6', 'ipv6', 'absolute', 1, 1]
        ]},
    'http_method': {
        'options': [None, 'Requests Per HTTP Method', 'requests/s', 'http methods', 'web_log.http_method', 'stacked'],
        'lines': [
        ]},
    'requests_per_ipproto': {
        'options': [None, 'Requests Per IP Protocol', 'requests/s', 'ip protocols', 'web_log.requests_per_ipproto',
                    'stacked'],
        'lines': [
            ['req_ipv4', 'ipv4', 'incremental', 1, 1],
            ['req_ipv6', 'ipv6', 'incremental', 1, 1]
        ]},
    'response_statuses': {
        'options': [None, 'Response Statuses', 'requests/s', 'responses', 'web_log.response_statuses',
                    'stacked'],
        'lines': [
            ['successful_requests', 'success', 'incremental', 1, 1],
            ['server_errors', 'error', 'incremental', 1, 1],
            ['redirects', 'redirect', 'incremental', 1, 1],
            ['bad_requests', 'bad', 'incremental', 1, 1],
            ['other_requests', 'other', 'incremental', 1, 1]
        ]}
}

NAMED_URL_PATTERN = namedtuple('URL_PATTERN', ['description', 'pattern'])


class Service(LogService):
    def __init__(self, configuration=None, name=None):
        """
        :param configuration:
        :param name:
        # self._get_data = None  # will be assigned in 'check' method.
        # self.order = None  # will be assigned in 'create_*_method' method.
        # self.definitions = None  # will be assigned in 'create_*_method' method.
        # self.detailed_chart = None  # will be assigned in 'create_*_method' method.
        # self.http_method_chart = None  # will be assigned in 'create_*_method' method.
        """
        LogService.__init__(self, configuration=configuration, name=name)
        # Variables from module configuration file
        self.log_path = self.configuration.get('path')
        self.detailed_response_codes = self.configuration.get('detailed_response_codes', True)
        self.all_time = self.configuration.get('all_time', True)
        self.url_pattern = self.configuration.get('categories')  # dict
        self.custom_log_format = self.configuration.get('custom_log_format')  # dict
        # Instance variables
        self.unique_all_time = list()  # sorted list of unique IPs
        self.regex = None  # will be assigned in 'find_regex' or 'find_regex_custom' method
        self.resp_time_func = None  # will be assigned in 'find_regex' or 'find_regex_custom' method
        self.data = {'bytes_sent': 0, 'resp_length': 0, 'resp_time_min': 0, 'resp_time_max': 0,
                     'resp_time_avg': 0, 'unique_cur_ipv4': 0, 'unique_cur_ipv6': 0, '2xx': 0,
                     '5xx': 0, '3xx': 0, '4xx': 0, '1xx': 0, '0xx': 0, 'unmatched': 0, 'req_ipv4': 0,
                     'req_ipv6': 0, 'unique_tot_ipv4': 0, 'unique_tot_ipv6': 0, 'successful_requests': 0,
                     'redirects': 0, 'bad_requests': 0, 'server_errors': 0, 'other_requests': 0}

    def check(self):
        """
        :return: bool

        We need to make sure:
        1. "log_path" is specified in the module configuration file
        2. "log_path" must be readable by netdata user and must exist
        3. "log_path' must not be empty. We need at least 1 line to find appropriate pattern to parse
        4. Plugin can work using predefined patterns (OK for nginx, apache default log format) or user defined
         pattern. So we need to check if we can parse last line from log file with user pattern OR module patterns.
        5. All patterns for per_url_request_counter feature are valid regex expressions
        """
        if not self.log_path:
            self.error('log path is not specified')
            return False

        if not access(self.log_path, R_OK):
            self.error('%s not readable or not exist' % self.log_path)
            return False

        if not getsize(self.log_path):
            self.error('%s is empty' % self.log_path)
            return False

        # Read last line (or first if there is only one line)
        with open(self.log_path, 'rb') as logs:
            logs.seek(-2, 2)
            while logs.read(1) != b'\n':
                logs.seek(-2, 1)
                if logs.tell() == 0:
                    break
            last_line = logs.readline()

        try:
            last_line = last_line.decode()
        except UnicodeDecodeError:
            try:
                last_line = last_line.decode(encoding='utf-8')
            except (TypeError, UnicodeDecodeError) as error:
                self.error(str(error))
                return False

        # Custom_log_format or predefined log format.
        if self.custom_log_format:
            match_dict, log_name, error = self.find_regex_custom(last_line)
        else:
            match_dict, log_name, error = self.find_regex(last_line)

        # "match_dict" is None if there are any problems
        if match_dict is None:
            self.error(str(error))
            return False

        # self.url_pattern check
        if self.url_pattern:
            self.url_pattern = check_req_per_url_pattern(self.url_pattern)

        # Double check
        if not (self.regex and self.resp_time_func):
            self.error('That can not happen, but it happened. "regex" or "resp_time_func" is None')

        # All is ok. We are about to start.
        if log_name == 'web_access':
            self.create_access_charts(match_dict)  # Create charts
            self._get_data = self._get_access_data
            self.info('Collected data: %s' % list(match_dict.keys()))
            return True
        else:
            # If it's not access_logs.. Not used at the moment
            return False

    def find_regex_custom(self, last_line):
        """
        :param last_line: str: literally last line from log file
        :return: tuple where:
        [0]: dict or None:  match_dict or None
        [1]: str or None: log_name or None
        [2]: str: error description

        We are here only if "custom_log_format" is in logs. We need to make sure:
        1. "custom_log_format" is a dict
        2. "pattern" in "custom_log_format" and pattern is <str> instance
        3. if "time_multiplier" is in "custom_log_format" it must be <int> instance

        If all parameters is ok we need to make sure:
        1. Pattern search is success
        2. Pattern search contains named subgroups (?P<subgroup_name>) (= "match_dict")

        If pattern search is success we need to make sure:
        1. All mandatory keys ['address', 'code', 'bytes_sent', 'method', 'url'] are in "match_dict"

        If this is True we need to make sure:
        1. All mandatory key values from "match_dict" have the correct format
         ("code" is integer, "method" is uppercase word, etc)

        If non mandatory keys in "match_dict" we need to make sure:
        1. All non mandatory key values from match_dict ['resp_length', 'resp_time'] have the correct format
         ("resp_length" is integer or "-", "resp_time" is integer or float)

        """
        if not is_dict(self.custom_log_format):
            return find_regex_return(msg='Custom log: "custom_log_format" is not a <dict>')

        pattern = self.custom_log_format.get('pattern')
        if not (pattern and isinstance(pattern, str)):
            return find_regex_return(msg='Custom log: "pattern" option is not specified or type is not <str>')

        resp_time_func = self.custom_log_format.get('time_multiplier') or 0

        if not isinstance(resp_time_func, int):
            return find_regex_return(msg='Custom log: "time_multiplier" is not an integer')

        try:
            regex = re.compile(pattern)
        except re.error as error:
            return find_regex_return(msg='Pattern compile error: %s' % str(error))

        match = regex.search(last_line)
        if match:
            match_dict = match.groupdict() or None
        else:
            return find_regex_return(msg='Custom log: pattern search FAILED')

        if match_dict is None:
            find_regex_return(msg='Custom log: search OK but contains no named subgroups'
                                  ' (you need to use ?P<subgroup_name>)')
        else:
            basic_values = {'address', 'method', 'url', 'code', 'bytes_sent'} - set(match_dict)

            if basic_values:
                return find_regex_return(msg='Custom log: search OK but some mandatory keys (%s) are missing'
                                         % list(basic_values))
            else:
                if not re.search(r'[\da-f.:]+', match_dict['address']):
                    return find_regex_return(msg='Custom log: can\'t parse "address": %s'
                                                 % match_dict['address'])
                if not re.search(r'[1-9]\d{2}', match_dict['code']):
                    return find_regex_return(msg='Custom log: can\'t parse "code": %s'
                                                 % match_dict['code'])
                if not re.search(r'[A-Z]+', match_dict['method']):
                    return find_regex_return(msg='Custom log: can\'t parse "method": %s'
                                                 % match_dict['method'])
                if not re.search(r'\d+|-', match_dict['bytes_sent']):
                    return find_regex_return(msg='Custom log: can\'t parse "bytes_sent": %s'
                                                 % match_dict['bytes_sent'])

            if 'resp_length' in match_dict:
                if not re.search(r'\d+', match_dict['resp_length']):
                    return find_regex_return(msg='Custom log: can\'t parse "resp_length": %s'
                                                 % match_dict['resp_length'])

            if 'resp_time' in match_dict:
                if not re.search(r'[\d.]+', match_dict['resp_length']):
                    return find_regex_return(msg='Custom log: can\'t parse "resp_time": %s'
                                                 % match_dict['resp_time'])
                else:
                    if '.' in match_dict['resp_time']:
                        self.resp_time_func = lambda time: time * (resp_time_func or 1000000)
                    else:
                        self.resp_time_func = lambda time: time * (resp_time_func or 1)

            self.regex = regex
            return find_regex_return(match_dict=match_dict,
                                     log_name='web_access',
                                     msg='We are fine')

    def find_regex(self, last_line):
        """
        :param last_line: str: literally last line from log file
        :return: tuple where:
        [0]: dict or None:  match_dict or None
        [1]: str or None: log_name or None
        [2]: str: error description
        We need to find appropriate pattern for current log file
        All logic is do a regex search through the string for all predefined patterns
        until we find something or fail.
        """
        # REGEX: 1.IPv4 address 2.HTTP method 3. URL 4. Response code
        # 5. Bytes sent 6. Response length 7. Response process time
        acs_default = re.compile(r'(?P<address>[\da-f.:]+)'
                                 r' -.*?"(?P<method>[A-Z]+)'
                                 r' (?P<url>.*?)"'
                                 r' (?P<code>[1-9]\d{2})'
                                 r' (?P<bytes_sent>\d+|-)')

        acs_apache_ext_insert = re.compile(r'(?P<address>[\da-f.:]+)'
                                           r' -.*?"(?P<method>[A-Z]+)'
                                           r' (?P<url>.*?)"'
                                           r' (?P<code>[1-9]\d{2})'
                                           r' (?P<bytes_sent>\d+|-)'
                                           r' (?P<resp_length>\d+)'
                                           r' (?P<resp_time>\d+) ')

        acs_apache_ext_append = re.compile(r'(?P<address>[\da-f.:]+)'
                                           r' -.*?"(?P<method>[A-Z]+)'
                                           r' (?P<url>.*?)"'
                                           r' (?P<code>[1-9]\d{2})'
                                           r' (?P<bytes_sent>\d+|-)'
                                           r' .*?'
                                           r' (?P<resp_length>\d+)'
                                           r' (?P<resp_time>\d+)'
                                           r'(?: |$)')

        acs_nginx_ext_insert = re.compile(r'(?P<address>[\da-f.:]+)'
                                          r' -.*?"(?P<method>[A-Z]+)'
                                          r' (?P<url>.*?)"'
                                          r' (?P<code>[1-9]\d{2})'
                                          r' (?P<bytes_sent>\d+)'
                                          r' (?P<resp_length>\d+)'
                                          r' (?P<resp_time>\d\.\d+) ')

        acs_nginx_ext_append = re.compile(r'(?P<address>[\da-f.:]+)'
                                          r' -.*?"(?P<method>[A-Z]+)'
                                          r' (?P<url>.*?)"'
                                          r' (?P<code>[1-9]\d{2})'
                                          r' (?P<bytes_sent>\d+)'
                                          r' .*?'
                                          r' (?P<resp_length>\d+)'
                                          r' (?P<resp_time>\d\.\d+)')

        def func_usec(time):
            return time

        def func_sec(time):
            return time * 1000000

        r_regex = [acs_apache_ext_insert, acs_apache_ext_append, acs_nginx_ext_insert,
                   acs_nginx_ext_append, acs_default]
        r_function = [func_usec, func_usec, func_sec, func_sec, func_usec]
        regex_function = zip(r_regex, r_function)

        match_dict = dict()
        for regex, function in regex_function:
            match = regex.search(last_line)
            if match:
                self.regex = regex
                self.resp_time_func = function
                match_dict = match.groupdict()
                break

        return find_regex_return(match_dict=match_dict or None,
                                 log_name='web_access',
                                 msg='Unknown log format. You need to use "custom_log_format" feature.')

    def create_access_charts(self, match_dict):
        """
        :param match_dict: dict: regex.search.groupdict(). Ex. {'address': '127.0.0.1', 'code': '200', 'method': 'GET'}
        :return:
        Create additional charts depending on the 'match_dict' keys and configuration file options
        1. 'time_response' chart is removed if there is no 'resp_time' in match_dict.
        2. Other stuff is just remove/add chart depending on yes/no in conf
        """
        def find_job_name(override_name, name):
            """
            :param override_name: str: 'name' var from configuration file
            :param name: str: 'job_name' from configuration file
            :return: str: new job name
            We need this for dynamic charts. Actually same logic as in python.d.plugin.
            """
            add_to_name = override_name or name
            if add_to_name:
                return '_'.join(['web_log', re.sub('\s+', '_', add_to_name)])
            else:
                return 'web_log'

        self.order = ORDER[:]
        self.definitions = deepcopy(CHARTS)

        job_name = find_job_name(self.override_name, self.name)
        self.detailed_chart = 'CHART %s.detailed_response_codes ""' \
                              ' "Detailed Response Codes" requests/s responses' \
                              ' web_log.detailed_response_codes stacked 1 %s\n' % (job_name, self.update_every)
        self.http_method_chart = 'CHART %s.http_method' \
                                 ' "" "Requests Per HTTP Method" requests/s "http methods"' \
                                 ' web_log.http_method stacked 2 %s\n' % (job_name, self.update_every)

        # Remove 'request_time' chart from ORDER if resp_time not in match_dict
        if 'resp_time' not in match_dict:
            self.order.remove('response_time')
        # Remove 'clients_all' chart from ORDER if specified in the configuration
        if not self.all_time:
            self.order.remove('clients_all')
        # Add 'detailed_response_codes' chart if specified in the configuration
        if self.detailed_response_codes:
            self.order.append('detailed_response_codes')
            self.definitions['detailed_response_codes'] = {'options': [None, 'Detailed Response Codes', 'requests/s',
                                                                       'responses', 'web_log.detailed_response_codes',
                                                                       'stacked'],
                                                           'lines': []}

        # Add 'requests_per_url' chart if specified in the configuration
        if self.url_pattern:
            self.definitions['requests_per_url'] = {'options': [None, 'Requests Per Url', 'requests/s',
                                                                'urls', 'web_log.requests_per_url', 'stacked'],
                                                    'lines': [['other_url', 'other', 'incremental']]}
            for elem in self.url_pattern:
                self.definitions['requests_per_url']['lines'].append([elem.description, elem.description,
                                                                      'incremental'])
                self.data.update({elem.description: 0})
            self.data.update({'other_url': 0})
        else:
            self.order.remove('requests_per_url')

    def add_new_dimension(self, dimension, line_list, chart_string, key):
        """
        :param dimension: str: response status code. Ex.: '202', '499'
        :param line_list: list: Ex.: ['202', '202', 'incremental']
        :param chart_string: Current string we need to pass to netdata to rebuild the chart
        :param key: str: CHARTS dict key (chart name). Ex.: 'response_time'
        :return: str: new chart string = previous + new dimensions
        """
        self.data.update({dimension: 0})
        # SET method check if dim in _dimensions
        self._dimensions.append(dimension)
        # UPDATE method do SET only if dim in definitions
        self.definitions[key]['lines'].append(line_list)
        chart = chart_string
        chart += "%s %s\n" % ('DIMENSION', ' '.join(line_list))
        print(chart)
        return chart

    def _get_access_data(self):
        """
        Parse new log lines
        :return: dict OR None
        None if _get_raw_data method fails.
        In all other cases - dict.
        """
        raw = self._get_raw_data()
        if raw is None:
            return None

        request_time, unique_current = list(), list()
        request_counter = {'count': 0, 'sum': 0}
        ip_address_counter = {'unique_cur_ip': 0}
        for line in raw:
            match = self.regex.search(line)
            if match:
                match_dict = match.groupdict()
                try:
                    code = ''.join([match_dict['code'][0], 'xx'])
                    self.data[code] += 1
                except KeyError:
                    self.data['0xx'] += 1
                # detailed response code
                if self.detailed_response_codes:
                    self._get_data_detailed_response_codes(match_dict['code'])
                # response statuses
                self._get_data_statuses(match_dict['code'])
                # requests per url
                if self.url_pattern:
                    self._get_data_per_url(match_dict['url'])
                # requests per http method
                self._get_data_http_method(match_dict['method'])
                # bandwidth sent
                bytes_sent = match_dict['bytes_sent'] if '-' not in match_dict['bytes_sent'] else 0
                self.data['bytes_sent'] += int(bytes_sent)
                # request processing time and bandwidth received
                if 'resp_length' in match_dict:
                    self.data['resp_length'] += int(match_dict['resp_length'])
                if 'resp_time' in match_dict:
                    resp_time = self.resp_time_func(float(match_dict['resp_time']))
                    bisect.insort_left(request_time, resp_time)
                    request_counter['count'] += 1
                    request_counter['sum'] += resp_time
                # requests per ip proto
                proto = 'ipv4' if '.' in match_dict['address'] else 'ipv6'
                self.data['req_' + proto] += 1
                # unique clients ips
                if address_not_in_pool(self.unique_all_time, match_dict['address'],
                                       self.data['unique_tot_ipv4'] + self.data['unique_tot_ipv6']):
                        self.data['unique_tot_' + proto] += 1
                if address_not_in_pool(unique_current, match_dict['address'], ip_address_counter['unique_cur_ip']):
                        self.data['unique_cur_' + proto] += 1
                        ip_address_counter['unique_cur_ip'] += 1
            else:
                self.data['unmatched'] += 1

        # timings
        if request_time:
            self.data['resp_time_min'] += int(request_time[0])
            self.data['resp_time_avg'] += int(round(float(request_counter['sum']) / request_counter['count']))
            self.data['resp_time_max'] += int(request_time[-1])
        return self.data

    def _get_data_detailed_response_codes(self, code):
        """
        :param code: str: CODE from parsed line. Ex.: '202, '499'
        :return:
        Calls add_new_dimension method If the value is found for the first time
        """
        if code not in self.data:
            chart_string_copy = self.detailed_chart
            self.detailed_chart = self.add_new_dimension(code, [code, code, 'incremental'],
                                                         chart_string_copy, 'detailed_response_codes')
        self.data[code] += 1

    def _get_data_http_method(self, method):
        """
        :param method: str: METHOD from parsed line. Ex.: 'GET', 'POST'
        :return:
        Calls add_new_dimension method If the value is found for the first time
        """
        if method not in self.data:
            chart_string_copy = self.http_method_chart
            self.http_method_chart = self.add_new_dimension(method, [method, method, 'incremental'],
                                                            chart_string_copy, 'http_method')
        self.data[method] += 1

    def _get_data_per_url(self, url):
        """
        :param url: str: URL from parsed line
        :return:
        Scan through string looking for the first location where patterns produce a match for all user
        defined patterns
        """
        match = None
        for elem in self.url_pattern:
            if elem.pattern.search(url):
                self.data[elem.description] += 1
                match = True
                break
        if not match:
            self.data['other_url'] += 1

    def _get_data_statuses(self, code):
        """
        :param code: str: response status code. Ex.: '202', '499'
        :return:
        """
        code_class = code[0]
        if code_class == '2' or code == '304' or code_class == '1':
            self.data['successful_requests'] += 1
        elif code_class == '3':
            self.data['redirects'] += 1
        elif code_class == '4':
            self.data['bad_requests'] += 1
        elif code_class == '5':
            self.data['server_errors'] += 1
        else:
            self.data['other_requests'] += 1


def address_not_in_pool(pool, address, pool_size):
    """
    :param pool: list of ip addresses
    :param address: ip address
    :param pool_size: current pool size
    :return: True if address not in pool. False if address in pool.
    """
    index = bisect.bisect_left(pool, address)
    if index < pool_size:
        if pool[index] == address:
            return False
        else:
            bisect.insort_left(pool, address)
            return True
    else:
        bisect.insort_left(pool, address)
        return True


def find_regex_return(match_dict=None, log_name=None, msg='Generic error message'):
    """
    :param match_dict: dict: re.search.groupdict() or None
    :param log_name: str: log name
    :param msg: str: error description
    :return: tuple:
    """
    return match_dict, log_name, msg


def check_req_per_url_pattern(url_pattern):
    """
    :param url_pattern: dict: ex. {'dim1': 'pattern1>', 'dim2': '<pattern2>'}
    :return: list of named tuples or None:
     We need to make sure all patterns are valid regular expressions
    """
    if not is_dict(url_pattern):
        return None

    result = list()

    def is_valid_pattern(pattern):
        """
        :param pattern: str
        :return: re.compile(pattern) or None
        """
        if not isinstance(pattern, str):
            return False
        else:
            try:
                compile_pattern = re.compile(pattern)
            except re.error:
                return False
            else:
                return compile_pattern

    for dimension, regex in url_pattern.items():
        valid_pattern = is_valid_pattern(regex)
        if isinstance(dimension, str) and valid_pattern:
            result.append(NAMED_URL_PATTERN(description=dimension, pattern=valid_pattern))

    return result or None


def is_dict(obj):
    """
    :param obj: dict:
    :return: True or False
    obj can be <dict> or <OrderedDict>
    """
    try:
        obj.keys()
    except AttributeError:
        return False
    else:
        return True
