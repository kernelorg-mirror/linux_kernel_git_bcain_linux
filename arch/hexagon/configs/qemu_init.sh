#!/bin/sh
/bin/busybox mount -t proc proc /proc
/bin/busybox mount -t sysfs sysfs /sys
/bin/busybox mount -t devtmpfs devtmpfs /dev
/bin/busybox --install -s /bin

# Start FastRPC dispatcher in background
if [ -e /dev/fastrpc_device ] && [ -x /bin/fastrpc_dispatcher ]; then
	/bin/fastrpc_dispatcher &
fi

echo "Boot successful"
exec /bin/sh
