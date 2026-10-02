# Log Download (Analyze View)

The _Log Download_ screen (**Analyze > Log Download**) is used to list (_Refresh_),
_Download_ and _Erase All_ log files from the connected vehicle.

![Analyze View Log Download](../../../assets/analyze/log_download.jpg)

## Live recording to a computer or external drive

Open **Analyze > Live Log Recording**, connect to the INS, select a destination using
**Browse…**, and click **Start Recording**. Before recording, increase `MAV_x_RATE` to
`200000` for the MAVLink instance used by the current connection.
The destination can be a local folder or a folder on a mounted external drive.
The application creates a timestamped `.ulg` file and displays its path and recorded byte count.
Click **Stop Recording** before removing the drive. A stop acknowledgement or a three-second
timeout closes the file. Connection and file errors are displayed in the recording status.

Recording uses the current MAVLink connection and the same ULog stream reconstruction
as `mavlink_ulog_streaming.py` in its default mode. It does not configure a dedicated
UDP streaming connection. The link must support MAVLink 2 and enough bandwidth for the log stream.
Host recordings are not automatically uploaded. Download and erase controls are disabled
while recording; recording continues when navigating away from this page.
