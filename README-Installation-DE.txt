Die Zip-Datei in ein Verzeichnis entpacken.
Die Muster-Konfiguration "server-dist.cfg" in "server.cfg" umbenennen oder kopieren.
Dann die Einstellungen in der Datei vornehmen.
Nach jeder Änderung in der sercver.cfg muss der Server neu gestartet werden, oder die Konfiguration neu geladen werden.

Um den neuen Server zu installieren und starten, musst du eine Kommandozeile mit Administrator Rechten öffnen und in das Verzeichnis wechseln in dem du die Dateien ausgepackt hast.
Eingabe von "Http2Serv.exe -i"  installiert den Dienst.
Eingabe von "Http2Serv.exe -s"  startet den Dienst

Zum Ausschalten und Deinstallieren:
Eingabe von "Http2Serv.exe -e"  stoppt den Dienst / Programm.
Eingabe von "Http2Serv.exe -r"  deinstalliert den Dienst

Eingabe von "Http2Serv.exe -k" ladet die Konfiguration neu, ohne den Server neu zu starten
Eingabe von "Http2Serv.exe -h" zeigt eine Auflistung aller Optionen


Eine minimale server.cfg sieht so aus:

[Listen]
0.0.0.0=80

[common]
RootDir = D:/Users/Thomas/html
LogFile = ./logs/default_access.log
ErrorLog = ./logs/default_error.log


Im Abschnitt [Listen] wird angegeben, auf welcher IP Adresse der Server mit welchem Port anfragen entgegen nimmt.

Alle Einstellungen im Abschnitt [common] sind die Default-Werte für alle anderen Abschnitte. Gibt es keinen weiteren
Abschnitte gelten die Einstellungen im Common global.

RootDir Ist das Verezeichnis in dem alle Dokumente liegen
LogFile und ErrorLog definieren die Logfiles. Sind eine oder beide Einstellungen nicht vorhanden, werden keine Log-Dateien geschrieben.

DefaultItem können ein, oder mehrere Dokumente angegeben werden die beim Aufruf einer URL ohne Dokument geladen werden.
DeflateTyps Content-Types die gezippt, gepackt übertragen werden. Beispiel: text/html text/plain text/xml text/css text/javascript application/javascript
            Content der schon komprimiert ist, sollte nicht noch mal komprimiert werden. Kostet CPU Zeit ohne wirklich kleiner zu werden. Beispiel Bilder / Images/ Zip-Dateien
AliasMatch  ändert das RootVerzeichnis, wenn die URL passt. Beispiel: ^/files(.*) D:/Users/Thomas/html/DateiStore$1
            Wenn http://meinserver.de/files/IrgendEineDatei.zip aufgerufen wird, wird die Datei im Verzeichnis "D:/Users/Thomas/html/DateiStore" gesucht anstelle dem normalen Root-Verzeichnis
RewriteRule
FileTyps    Damit kann gesteuert werden ob bestimmte Dateierweiterungen durch ein CGI-Programm verarbeitet wird. z.B.: php C:/Users/Thomas/Programme/php/php-cgi.exe	    
ForceType   Erzwingt einen Bestimmten Content-Type der gesendet wird unanhängig von der Dateierweiterung. 
Authenticate Damit kann man Verzeichnis mit Username/Passwort schützen
SetEnvIf    Setzt eine Enviroment-Variable
AddHeader   Sendet immer einen Zusätzlichen Header bei der Antwort zurück

VirtualHost Eine Liste der virtuellen Host
SSL         Der virtuelle Host verwendet SSL/TLS
KeyFile	    Zertifikat Schlüsseldatei
CertFile    Zertifikatsdatei
CaBundle    Rootzertifikat der Ausstellungsbehörde
SSLCipher   Optional.


MsgDir	    Verzeichnis in dem die Msg Dateien liegen. Default ist Msg
RedirectMatch Redirekt die Anfrage zu einer neuen URL

ScriptAliasMatch wenn die URL den Suchstring enthält wird das angebene CGI Programm aufgerufen
ScriptOptionsHdl wenn die URL den Suchstring enthält beantwortet das CGI Programm den OPTIONS anfrage
ScriptAuthHdl    wenn die URL den Suchstring enthält verarbeitet das CGI Programm die Authetifizierung




