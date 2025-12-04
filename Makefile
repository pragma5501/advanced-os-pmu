KDIR := /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)
SRC := $(PWD)/src
SRC_KO := $(PWD)/ko
MODULES := part1.ko part3.ko

.PHONY: all modules clean

all: modules

modules:
	mkdir -p $(SRC_KO)
	$(MAKE) -C $(KDIR) M=$(SRC) modules
	cp $(addprefix $(SRC)/,$(MODULES)) $(SRC_KO)/
	$(MAKE) -C $(KDIR) M=$(SRC) clean

clean:
	rm -f $(MODULES)
	rm -rf $(SRC_KO)
	$(MAKE) -C $(KDIR) M=$(SRC) clean
