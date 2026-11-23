#---------------------------------------------------------------------------------------------------------------------
# Snowberry: the family Christmas GBA game. Built with Butano (butano/ is a git submodule).
# Use ./build.sh (Mac/Linux) or .\build.ps1 (Windows) instead of calling make directly.
#---------------------------------------------------------------------------------------------------------------------
TARGET      	:=  snowberry
BUILD       	:=  build
LIBBUTANO   	:=  butano/butano
PYTHON      	?=  python3
SOURCES     	:=  src src/games
INCLUDES    	:=  include
DATA        	:=
GRAPHICS    	:=  graphics/ui graphics/characters graphics/crowd graphics/world graphics/flappy graphics/pingpong \
                    graphics/sled graphics/catch graphics/fishing graphics/horse
AUDIO       	:=  audio
AUDIOBACKEND	:=  maxmod
AUDIOTOOL		:=
DMGAUDIO    	:=
DMGAUDIOBACKEND	:=  default
ROMTITLE    	:=  SNOWBERRY
ROMCODE     	:=  ZSBE
USERFLAGS   	:=
USERCXXFLAGS	:=
USERASFLAGS 	:=
USERLDFLAGS 	:=
USERLIBDIRS 	:=
USERLIBS    	:=
DEFAULTLIBS 	:=
STACKTRACE		:=
USERBUILD   	:=
EXTTOOL     	:=

ifndef LIBBUTANOABS
	export LIBBUTANOABS	:=	$(realpath $(LIBBUTANO))
endif

include $(LIBBUTANOABS)/butano.mak
