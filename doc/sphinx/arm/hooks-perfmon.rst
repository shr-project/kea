.. ischooklib:: libdhcp_perfmon.so
.. _hooks-perfmon:

``libdhcp_perfmon.so``: PerfMon
===============================

This hook library can be loaded by either kea-dhcp4 or kea-dhcp6 servers
to extend them with the ability to track and report performance related data.

.. note::

    This library is experimental and not recommended for use in production
    environments.

Overview
~~~~~~~~

The library, added in Kea 2.5.6, can be loaded by the :iscman:`kea-dhcp4` or
:iscman:`kea-dhcp6` daemon by adding it to the ``hooks-libraries`` element of
the server's configuration:

.. code-block:: javascript

    {
        "hooks-libraries": [
            {
                "library": "/usr/local/lib/libdhcp_perfmon.so",
                "parameters": {
                    ...
                }
            },
            ...
        ],
        ...
    }

It tracks the life cycle of client query packets as they are processed by Kea,
beginning with when the query was received by the kernel to when the response
is ready to be sent.  This tracking is driven by a packet event stack which
contains a list of event/timestamp pairs for significant milestones that
occur during the processing of a client query.  The list of possible events is
shown below:

#. socket_received - Kernel placed the client query into the socket buffer
#. buffer_read - Server read the client query from the socket buffer
#. mt_queued - Server placed the client query into its thread-pool work queue (MT mode only)
#. process_started - Server has begun processing the query
#. process_completed - Server has constructed the response and is ready to send it

This list is likely to expand over time. It will also be possible for hook developers
to add their own events. This will be detailed in a future release.

Passive Event Logging
~~~~~~~~~~~~~~~~~~~~~

As long as the PerfMon hook library is loaded it will log the packet event stack
contents for each client query which generates a response packet.  The log entry
will contain client query identifiers followed the list of event/timestamp pairs
as they occurred in the order they occurred.

For :iscman:`kea-dhcp4` the log is identified by the label, ``PERFMON_DHCP4_PKT_EVENTS``,
and emitted at logger debug level 50 or higher. For a DHCPDISCOVER it is emitted
once the DHCPOFFER is ready to send and will look similar to the following (see
the second entry)::

   2024-03-20 10:52:20.069 INFO  [kea-dhcp4.leases/50033.140261252249344] DHCP4_LEASE_OFFER [hwtype=1 08:00:27:25:d3:f4], cid=[no info], tid=0xc288f9: lease 178.16.2.0 will be offered
   2024-03-20 10:52:20.070 DEBUG [kea-dhcp4.perfmon-hooks/50033.140261252249344] PERFMON_DHCP4_PKT_EVENTS query: [hwtype=1 08:00:27:25:d3:f4], cid=[no info], tid=0xc288f9 events=[2024-Mar-20 14:52:20.067563 : socket_received, 2024-Mar-20 14:52:20.067810 : buffer_read, 2024-Mar-20 14:52:20.067897 : mt_queued, 2024-Mar-20 14:52:20.067952 : process_started, 2024-Mar-20 14:52:20.069614 : process_completed]

..

For :iscman:`kea-dhcp6` the log is identified by the label, ``PERFMON_DHCP6_PKT_EVENTS``,
and emitted only at logger debug level 50 or higher. For a DHCPV6_SOLICIT it is emitted
once the DHCPV6_ADVERTISE is ready to send and will look similar to the following (see
the second entry)::

   2024-03-20 10:22:14.030 INFO  [kea-dhcp6.leases/47195.139913679886272] DHCP6_LEASE_ADVERT duid=[00:03:00:01:08:00:27:25:d3:f4], [no hwaddr info], tid=0xb54806: lease for address 3002:: and iaid=11189196 will be advertised
   2024-03-20 10:22:14.031 DEBUG [kea-dhcp6.perfmon-hooks/47195.139913679886272] PERFMON_DHCP6_PKT_EVENTS query: duid=[00:03:00:01:08:00:27:25:d3:f4], [no hwaddr info], tid=0xb54806 events=[2024-Mar-20 14:22:14.028729 : socket_received, 2024-Mar-20 14:22:14.028924 : buffer_read, 2024-Mar-20 14:22:14.029005 : process_started, 2024-Mar-20 14:22:14.030566 : process_completed]

..

Duration Monitoring
~~~~~~~~~~~~~~~~~~~

When monitoring is enabled, stack event data will be aggregated over a specified interval. These
aggregates are referred to as monitored durations or simply durations for ease. Durations are
uniquely identified by a "duration key" which consists of the following values:

* query message type - Message type of the client query (e.g.DHCPDISCOVER, DHCPV6_REQUEST)
* response message type - Message type of the server response (e.g. DHCPOFFER, DHCPV6_REPLY)
* start event label - Event that defines the beginning of the task (e.g. socket_received, process_started)
* stop event label - Event that defines the end of the task (e.g. buffer_read, process_completed)
* subnet id - subnet selected during message processing (or 0 for global durations)

Once the server has finished constructing a response to a query, the query's event stack
is processed into a series of updates to monitored durations.  If upon updating, a
duration's sample interval is found to have been completed, it is sent to reporting
and a new sample interval is begun.  The interval width is dictacted by configuration
parameter ``interval-width-secs``.

The event stack for the multi-threaded mode DHCPDISCOVER/DHCPOFFER cycle shown above
contains the following events:

    +-----------------------------+--------------------+
    | Event Timestamp             | Event Label        |
    +=============================+====================+
    | 2024-Mar-20 14:52:20.067563 | socket_received    |
    +-----------------------------+--------------------+
    | 2024-Mar-20 14:52:20.067810 | buffer_read        |
    +-----------------------------+--------------------+
    | 2024-Mar-20 14:52:20.067897 | mt_queued          |
    +-----------------------------+--------------------+
    | 2024-Mar-20 14:52:20.067952 | process_started    |
    +-----------------------------+--------------------+
    | 2024-Mar-20 14:52:20.069614 | process_completed  |
    +-----------------------------+--------------------+

Assuming the selected subnet's ID was 100, the duration updates formed by PerfMon
from these events are shown below:

    +--------------------------------------------------------------+--------------+
    | Duration Keys for SubnetID 100                               | Update in    |
    |                                                              | microseconds |
    +==============================================================+==============+
    | DHCPDISCOVER.DHCPOFFER.socket_received-buffer_read.100       |          247 |
    +--------------------------------------------------------------+--------------+
    | DHCPDISCOVER.DHCPOFFER.buffer_read-mt_queue.100              |           87 |
    +--------------------------------------------------------------+--------------+
    | DHCPDISCOVER.DHCPOFFER.mt_queued-process_started.100         |           55 |
    +--------------------------------------------------------------+--------------+
    | DHCPDISCOVER.DHCPOFFER.process_started-process_completed.100 |         1662 |
    +--------------------------------------------------------------+--------------+
    | DHCPDISCOVER.DHCPOFFER.composite-total_response.100          |         2051 |
    +--------------------------------------------------------------+--------------+

Notice that in addition to the adjacent event updates, there is an additional duration
update for the total duration of the entire stack whose key contains the event-pair
``composite-total_response``.  This tracks the total time to responds from query
received until the response is ready to send.  Finally, there would also be global
duration updates for each of the above:

    +--------------------------------------------------------------+--------------+
    |  Global Duration Keys                                        | Update in    |
    |                                                              | milliseconds |
    +==============================================================+==============+
    | DHCPDISCOVER.DHCPOFFER.socket_received-buffer_read.0         |          247 |
    +--------------------------------------------------------------+--------------+
    | DHCPDISCOVER.DHCPOFFER.buffer_read-mt_queue.0                |           87 |
    +--------------------------------------------------------------+--------------+
    | DHCPDISCOVER.DHCPOFFER.mt_queued-process_started.0           |           55 |
    +--------------------------------------------------------------+--------------+
    | DHCPDISCOVER.DHCPOFFER.process_started-process_completed.0   |         1662 |
    +--------------------------------------------------------------+--------------+
    | DHCPDISCOVER.DHCPOFFER.composite-total_response.0            |         2051 |
    +--------------------------------------------------------------+--------------+

Statistics Reporting
~~~~~~~~~~~~~~~~~~~~

When enabled (see ``stats-mgr-reporting``), PerfMon will report a duration's data
each time the duration completes a sampling interval.  Each statistic employs the
following naming convention:

::

    {subnet-id[x]}.perfmon.<query type>-<response type>.<start event>-<end event>.<value-name>

And there will be both a global and a subnet-specific value for each.  Currently the only
value reported for a given duration key is "average-ms".  This statistic is the average time
between the duration's event pair over the most recently completed interval.  In other
words if during a given interval there were 7 occurrences (i.e. updates) totaling
350ms, the the average-ms reported would be 50ms.  Continuing with example above, the
statistics reported would be named as follows for subnet level values:

::

    subnet[100].perfmon.DHCPDISCOVER.DHCPOFFER.socket_received-buffer_read.average-ms
    subnet[100].perfmon.DHCPDISCOVER.DHCPOFFER.buffer_read-mt_queue.average-ms
    subnet[100].perfmon.DHCPDISCOVER.DHCPOFFER.mt_queued-process_started.average-ms
    subnet[100].perfmon.DHCPDISCOVER.DHCPOFFER.process_started-process_completed.average-ms
    subnet[100].perfmon.DHCPDISCOVER.DHCPOFFER.composite-total_response.average-ms

and as shown for global values:

::

    perfmon.DHCPDISCOVER.DHCPOFFER.socket_received-buffer_read.average-ms
    perfmon.DHCPDISCOVER.DHCPOFFER.buffer_read-mt_queue.average-ms
    perfmon.DHCPDISCOVER.DHCPOFFER.mt_queued-process_started.average-ms
    perfmon.DHCPDISCOVER.DHCPOFFER.process_started-process_completed.average-ms
    perfmon.DHCPDISCOVER.DHCPOFFER.composite-total_response.average-ms

Since they are reported to StatsMgr they may be fetched using the commands :isccmd:`statistic-get-all`
or :isccmd:`statistic-get`.

Alarms
~~~~~~

Alarms may be defined to watch specific durations. Each alarm defines a high-water mark,
``high-water-ms`` and a low-water mark, ``low-water-ms``.  When the reported average value
for duration exceeds the high-water mark, a WARN level alarm log will be emitted at which
point the alarm is considered "triggered".  Once triggered the WARN level log will be
repeated at a specified alarm report interval, ``alarm-report-secs`` as long the reported
average for the duration remains above the low-water mark. Once the average falls below the
low-water mark the alarm is "cleared" and an INFO level log will be emitted.

The alarm triggered WARN log will look similar to the following:

::

    2024-03-20 10:22:14.030 WARN [kea-dhcp6.leases/47195.139913679886272] PERFMON_ALARM_TRIGGERED Alarm for DHCPDISCOVER.DHCPOFFER.composite-total_response.0 has been triggered since 2024-03-20 10:18:20.070000, reported average duration 00:00:00.700000 exceeds high-water-ms: 500


and the alarm cleared INFO log will look similar to the following:

::

     2024-03-20 10:30:14.030 INFO [kea-dhcp6.leases/47195.139913679886272] PERFMON_ALARM_CLEARED Alarm for DHCPDISCOVER.DHCPOFFER.composite-total_response.0 has been cleared, reported average duration 00:00:00.010000 is now below low-water-ms: 25

API Commands
~~~~~~~~~~~~

    Commands to enable or disable monitoring, clear or alter alarms, and fetch duration data
    are anticipated but not yet supported.

Configuration
~~~~~~~~~~~~~

An example of the anticipated configuration is shown below:

.. code-block:: javascript

    {
        "hooks-libraries": [
        {
            "library": "lib/kea/hooks/libdhcp_perfmon.so",
            "parameters": {
                "enable-monitoring": true,
                "interval-width-secs": 5,
                "stats-mgr-reporting": true,
                "alarm-report-secs": 600,
                "alarms": [
                {
                    "duration-key": {
                        "query-type": "DHCPDISCOVER",
                        "response-type": "DHCPOFFER",
                        "start-event": "process-started",
                        "stop-event": "process-completed",
                        "subnet-id": 0
                    },
                    "enable-alarm": true,
                    "high-water-ms": 500,
                    "low-water-ms": 25
                }]
            }
        }]
    }

Where:

* enable-monitoring
    Enables event data aggregation for reporting, statistics, and alarms. Defaults to false.
* interval-width-secs
    The amount of time, in seconds, that individual task durations are accumulated into an
    aggregate before it is reported. Default is 60 seconds.
* stats-mgr-reporting
    Enables reporting aggregates to StatsMgr. Defaults to true.
* alarm-report-secs
    The amount of time, in seconds, between logging for an alarm once it has been triggered.
    Defaults to 300 seconds.
* alarms
    A optional list of alarms that monitor specific duration aggregates. Each alarm is
    defined by the following:

  * duration-key
        Identifies the monitored duration to watch

    * query-type - Message type of the client query (e.g.DHCPDISCOVER, DHCPV6_REQUEST)
    * response-type - Message type of the server response (e.g. DHCPOFFER, DHCPV6_REPLY)
    * start-event - Event that defines the beginning of the task (e.g. socket_received, process_started)
    * stop-event - Event that defines the end of the task
    * subnet-id - subnet selected during message processing (or 0 for global durations)

  * enable-alarm
        Enables or disables this alarm. Defaults to true.

  * high-water-ms
        The value, in milliseconds, that must be exceeded to trigger this alarm.
        Must be greater than zero.

  * low-water-ms
        The value, in milliseconds, that must be subceeded to clear this alarm
        Must be greater than zero but less than high-water-ms.

.. note::
    Passive event logging is always enabled, even without specifying the 'parameters' section.

