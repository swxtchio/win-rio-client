@echo off
For /F "tokens=2,3,4 delims=/ " %%A in ('Date /t') do (
  Set Day=%%A
  Set Month=%%B
  Set Year=%%C
  Set All=%%C%%B%%A
)
set "FullDate=%Year%%Month%%Day%"
For /F "tokens=1,2,3 delims=:,. " %%A in ('echo %time%') do (
  set Hour=%%A
  set Min=%%B
  set Sec=%%C
)
if %Hour% lss 10 (
  set "Hour=0%Hour%"
)
set "FullTime=%Hour%%Min%%Sec%"
echo %FullDate%.%FullTime%
@echo on