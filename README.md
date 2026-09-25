
## PingFinder Message Daemon

PingFinder Message Daemon is a background service for scheduling and sending
SMS messages to PingFinder service clients, with IPC-based message submission
and XMPP delivery status notifications for the administrator.

## User Management

Users are managed manually by editing the `clients.json` file.

To add or remove a user:

1. Edit `clients.json` and add or remove the corresponding user entry.
2. Save the file.
3. Restart the `pingfinder-msgd` service for the changes to take effect.
Alternatively, changes to `config.json` will be detected automatically
and applied within 5 minutes.

## Service Data

The service stores its runtime data in `/var/lib/pingfinder/msgd/`.

The directory contains files used to store the current schedule,
task execution statuses, client statuses, and IPC service data.

## Build

Create a build directory and configure the project with the desired build type:

~~~bash
mkdir build
cd build
cmake .. -DCMAKE_BUILD_TYPE=<build-type>
cmake --build . --parallel
~~~
`<build-type>` can be one of the following:
 - `Release` - optimized build intended for production use.
 - `Debug` - debug build with debugging information and without optimizations.

## Configuration

Configuration files are stored in `/etc/pingfinder/msgd/`:

- `clients.json`   — PingFinder clients
- `transport.json` — message transport configuration

Example configuration file examples are available in [`examples/config/`](examples/config/).

## License

This project is licensed under the GNU General Public License version 3 (GPL-3.0).

See the LICENSE file for the full license text.
