################################################################################
# Automatically-generated file. Do not edit!
################################################################################

SHELL := /bin/csh
RM := rm -rf

USER_OBJS :=

LIBS := 
PROJ := 

O_SRCS := 
C_SRCS := 
S_SRCS := 
S_UPPER_SRCS := 
OBJ_SRCS := 
ASM_SRCS := 
PREPROCESSING_SRCS := 
OBJS := 
OBJS_AS_ARGS := 
C_DEPS := 
C_DEPS_AS_ARGS := 
EXECUTABLES := 
OUTPUT_FILE_PATH :=
OUTPUT_FILE_PATH_AS_ARGS :=
AVR_APP_PATH :=$$$AVR_APP_PATH$$$
QUOTE := "
ADDITIONAL_DEPENDENCIES:=
OUTPUT_FILE_DEP:=

# Every subdirectory with source files must be described here
SUBDIRS := 


# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS +=  \
../cardtest.cpp \
../diskmenu.cpp \
../floppyemu.cpp \
../millitimer.cpp \
../noklcd.cpp \
../SdFat/Sd2Card.cpp \
../SdFat/SdBaseFile.cpp \
../SdFat/SdFat.cpp \
../SdFat/SdVolume.cpp \
../xsvf/lenval.cpp \
../xsvf/micro.cpp \
../xsvf/ports.cpp


PREPROCESSING_SRCS += 


ASM_SRCS += 


OBJS +=  \
cardtest.o \
diskmenu.o \
floppyemu.o \
millitimer.o \
noklcd.o \
Sd2Card.o \
SdBaseFile.o \
SdFat.o \
SdVolume.o \
lenval.o \
micro.o \
ports.o


OBJS_AS_ARGS +=  \
cardtest.o \
diskmenu.o \
floppyemu.o \
millitimer.o \
noklcd.o \
Sd2Card.o \
SdBaseFile.o \
SdFat.o \
SdVolume.o \
lenval.o \
micro.o \
ports.o


C_DEPS +=  \
cardtest.d \
diskmenu.d \
floppyemu.d \
millitimer.d \
noklcd.d \
Sd2Card.d \
SdBaseFile.d \
SdFat.d \
SdVolume.d \
lenval.d \
micro.d \
ports.d


C_DEPS_AS_ARGS +=  \
cardtest.d \
diskmenu.d \
floppyemu.d \
millitimer.d \
noklcd.d \
Sd2Card.d \
SdBaseFile.d \
SdFat.d \
SdVolume.d \
lenval.d \
micro.d \
ports.d


OUTPUT_FILE_PATH +=floppyemu.elf

OUTPUT_FILE_PATH_AS_ARGS +=floppyemu.elf

ADDITIONAL_DEPENDENCIES:=

OUTPUT_FILE_DEP:= ./makedep.mk

# AVR32/GNU C Compiler























./%.o: .././%.cpp
	@echo Building file: $<
	@echo Invoking: AVR8/GNU C++ Compiler
	$(QUOTE)C:\Program Files (x86)\Atmel\AVR Studio 5.1\extensions\Atmel\AVRGCC\3.3.1.27\AVRToolchain\bin\avr-g++.exe$(QUOTE) -funsigned-char -funsigned-bitfields -DF_CPU=20000000  -I"../SdFat" -I"../xsvf"  -O2 -ffunction-sections -fpack-struct -fshort-enums -Wall -c -MD -MP -MF "$(@:%.o=%.d)" -MT"$(@:%.o=%.d)"  -mmcu=atmega1284p   -o"$@" "$<" 
	@echo Finished building: $<

./%.o: ../SdFat/%.cpp
	@echo Building file: $<
	@echo Invoking: AVR8/GNU C++ Compiler
	$(QUOTE)C:\Program Files (x86)\Atmel\AVR Studio 5.1\extensions\Atmel\AVRGCC\3.3.1.27\AVRToolchain\bin\avr-g++.exe$(QUOTE) -funsigned-char -funsigned-bitfields -DF_CPU=20000000  -I"../SdFat" -I"../xsvf"  -O2 -ffunction-sections -fpack-struct -fshort-enums -Wall -c -MD -MP -MF "$(@:%.o=%.d)" -MT"$(@:%.o=%.d)"  -mmcu=atmega1284p   -o"$@" "$<" 
	@echo Finished building: $<

./%.o: ../xsvf/%.cpp
	@echo Building file: $<
	@echo Invoking: AVR8/GNU C++ Compiler
	$(QUOTE)C:\Program Files (x86)\Atmel\AVR Studio 5.1\extensions\Atmel\AVRGCC\3.3.1.27\AVRToolchain\bin\avr-g++.exe$(QUOTE) -funsigned-char -funsigned-bitfields -DF_CPU=20000000  -I"../SdFat" -I"../xsvf"  -O2 -ffunction-sections -fpack-struct -fshort-enums -Wall -c -MD -MP -MF "$(@:%.o=%.d)" -MT"$(@:%.o=%.d)"  -mmcu=atmega1284p   -o"$@" "$<" 
	@echo Finished building: $<



# AVR32/GNU Preprocessing Assembler



# AVR32/GNU Assembler




ifneq ($(MAKECMDGOALS),clean)
ifneq ($(strip $(C_DEPS)),)
-include $(C_DEPS)
endif
endif

# Add inputs and outputs from these tool invocations to the build variables 

# All Target
all: $(OUTPUT_FILE_PATH) $(ADDITIONAL_DEPENDENCIES)

$(OUTPUT_FILE_PATH): $(OBJS) $(USER_OBJS) $(OUTPUT_FILE_DEP)
	@echo Building target: $@
	@echo Invoking: AVR8/GNU C++ Linker
	$(QUOTE)C:\Program Files (x86)\Atmel\AVR Studio 5.1\extensions\Atmel\AVRGCC\3.3.1.27\AVRToolchain\bin\avr-g++.exe$(QUOTE) -o$(OUTPUT_FILE_PATH_AS_ARGS) $(OBJS_AS_ARGS) $(USER_OBJS) $(LIBS) -Wl,-Map="floppyemu.map" -Wl,-lm  -Wl,--gc-sections -mrelax -Wl,-section-start=.bootldrinfo=0x1eff8   -mmcu=atmega1284p  
	@echo Finished building target: $@
	"C:\Program Files (x86)\Atmel\AVR Studio 5.1\extensions\Atmel\AVRGCC\3.3.1.27\AVRToolchain\bin\avr-objcopy.exe" -O ihex -R .eeprom -R .fuse -R .lock -R .signature  "floppyemu.elf" "floppyemu.hex"
	"C:\Program Files (x86)\Atmel\AVR Studio 5.1\extensions\Atmel\AVRGCC\3.3.1.27\AVRToolchain\bin\avr-objcopy.exe" -O binary -R .eeprom -R .fuse -R .lock -R .signature  "floppyemu.elf" "femu.bin"
	"C:\Program Files (x86)\Atmel\AVR Studio 5.1\extensions\Atmel\AVRGCC\3.3.1.27\AVRToolchain\bin\avr-objcopy.exe" -j .eeprom  --set-section-flags=.eeprom=alloc,load --change-section-lma .eeprom=0  --no-change-warnings -O ihex "floppyemu.elf" "floppyemu.eep" || exit 0
	"C:\Program Files (x86)\Atmel\AVR Studio 5.1\extensions\Atmel\AVRGCC\3.3.1.27\AVRToolchain\bin\avr-objdump.exe" -h -S "floppyemu.elf" > "floppyemu.lss"
	srec_cat "floppyemu.hex" -intel "..\bootldr\bootldr\Release\bootldr.hex" -intel -o "merged.hex" -intel
	"C:\Program Files (x86)\Atmel\AVR Studio 5.1\extensions\Atmel\AVRGCC\3.3.1.27\AVRToolchain\bin\avr-size.exe" -C --mcu=atmega1284p  "floppyemu.elf"
	

	




# Other Targets
clean:
	-$(RM) $(OBJS_AS_ARGS)$(C_DEPS_AS_ARGS) $(EXECUTABLES) 
	rm -rf "floppyemu.hex" "floppyemu.lss" "floppyemu.eep" "floppyemu.map"
	