The adapted example

"iMX6DQ ISL79985 MIPI CSI2 CVBS camera surround view solution for Linux BSP"

https://community.freescale.com/docs/DOC-158531

for testing of tw6869

Test commands:

mxc_v4l2_tvin.out -ol 0 -ot 0 -ow 960 -oh 540 -d 1 -x 0 -g2d -m &

mxc_v4l2_tvin.out -ol 960 -ot 0 -ow 960 -oh 540 -d 1 -x 1 -g2d -m &

mxc_v4l2_tvin.out -ol 0 -ot 540 -ow 960 -oh 540 -d 1 -x 2 -g2d -m &

mxc_v4l2_tvin.out -ol 960 -ot 540 -ow 960 -oh 540 -d 1 -x 3 -g2d -m &