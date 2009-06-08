rem from http://www.debuginfo.com/tools/cdbbatch.html
rem Usage: dumpanalyze.bat wezdesk-XXXX-trace.dmp
rem Leaves output in out.txt
cdb -pv -z %1 -y "%CD%" -i "%CD%" -srcpath "%CD%;%CD%\.." -logo out.txt -lines -c "!sym prompts;.reload;!analyze -v;.ecxr;kPn;!for_each_frame dv /t; ~* kPn; q"
