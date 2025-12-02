KDIR := /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)
SRC := $(PWD)/src
SRC_KO := $(PWD)/ko
KO  := part1.ko

.PHONY: all clean $(KO)

all: $(KO)


$(KO):
	mkdir -p $(SRC_KO)
	$(MAKE) -C $(KDIR) M=$(SRC) modules
	cp $(SRC)/$(KO) $(SRC_KO)/$(KO)
	$(MAKE) -C $(KDIR) M=$(SRC) clean

clean:
	rm -f $(KO)
	rm -rf ${SRC_KO}
	$(MAKE) -C $(KDIR) M=$(SRC) clean
