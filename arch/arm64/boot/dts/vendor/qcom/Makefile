ifeq ($(CONFIG_BOARD_ALIOTH),y)
dtbo-$(CONFIG_BOARD_ALIOTH) += \
                alioth-sm8250-overlay.dtbo
alioth-sm8250-overlay.dtbo-base := kona-v2.1.dtb
endif

ifeq ($(CONFIG_BOARD_APOLLO),y)
dtbo-$(CONFIG_BOARD_APOLLO) += \
                apollo-sm8250-overlay.dtbo
apollo-sm8250-overlay.dtbo-base := kona-v2.1.dtb
endif

always		:= $(dtb-y)
subdir-y	:= $(dts-dirs)
clean-files    := *.dtb *.dtbo
