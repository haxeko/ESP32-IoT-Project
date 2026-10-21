#!/bin/sh
cd /tmp/route && rm -f board.ses fr.done
setsid sh -c "timeout ${2:-240} java -Djava.awt.headless=true -jar /tmp/fr/freerouting-2.1.0.jar -de board.dsn -do board.ses --router.max_passes=${1:-30} --gui.enabled=false > fr.log 2>&1; echo \$? > fr.done" > /dev/null 2>&1 < /dev/null &
