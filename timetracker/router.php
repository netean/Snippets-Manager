<?php
// Router for PHP's built-in web server, used by start.sh / start.bat when the
// app runs from a shared drive instead of a web host. Hides private files.
$path = rawurldecode((string) parse_url($_SERVER['REQUEST_URI'], PHP_URL_PATH));
if (preg_match('#(^|/)(lib|data)(/|$)|(^|/)(config|config\.sample|router)\.php$|\.(sqlite|db|sh|bat|md)$#i', $path)) {
    http_response_code(404);
    exit('Not found');
}
return false;
