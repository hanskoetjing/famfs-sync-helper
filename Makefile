obj-m += famfs_sync_helper.o
obj-m += get_cxl_range.o

BUILD='/lib/modules/$(shell uname -r)/build'

all:
	make -C ${BUILD} M=$(PWD) modules

clean:
	make -C ${BUILD} M=$(PWD) clean
