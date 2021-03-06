# $Id: app_lcd.mk 1201 2011-05-09 20:05:33Z tk $
# defines additional rules for application specific LCD driver

# enhance include path
C_INCLUDE +=	-I $(MIOS32_PATH)/modules/app_lcd/ssd1306

# add modules to thumb sources
THUMB_SOURCE += \
	$(MIOS32_PATH)/modules/app_lcd/ssd1306/app_lcd.c

# include fonts
include $(MIOS32_PATH)/modules/glcd_font/glcd_font.mk

# directories and files that should be part of the distribution (release) package
DIST += $(MIOS32_PATH)/modules/app_lcd/ssd1306

