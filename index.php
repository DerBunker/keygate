<?php

define('AUTH_TOKEN_USER', ''); // for app
define('AUTH_TOKEN_ADMIN', ''); // for key-receiver
define('KEY_SERVER_URI', ''); //
define('KEY_SERVER_POST_AUTH_TOKEN', ''); // Auth for key-receiver
define('KEY_SERVER_AUTH_TOKEN', ''); // another one token, choice between them in depend of your auth type
define('SQLITE_DB_PATH', '/www/db/keys.db');
define('AUTH_TYPE_USER', 1);
define('AUTH_TYPE_ADMIN', 2);
define('MIN_KEYS_STOCK', 50);
define('KEY_LENGTH', 66);
define('CURL_CONNECTION_TIMEOUT', 10);

$authType = null;

try {
    $pdo = initPdo();

    if ($argc > 1 && isset($argv[1])) {
        switch ($argv[1]) {
            case 'refill_keys':
                $authType = AUTH_TYPE_ADMIN;

                while (MIN_KEYS_STOCK > availableKeysCount($pdo)) {
                    requestAndSaveNewKey($pdo);
                }
                break;

            case 'gc':
                gc($pdo);
                break;
        }

        die;
    }

    httpAuth();

    $action = $_GET['action'];

    switch ($action) {
        case 'getkey':
            $key = nextKey($pdo);
            $availableCount = availableKeysCount($pdo);

            httpResponse(
                200,
                json_encode(
                    [
                        'response_date' => time(),
                        'key' => [
                            'created_at' => $key['created_at'],
                            'key_blob' => $key['keyblobcontent'],
                            'checksum' => $key['checksum'],
                        ],
                        'available_keys_count' => $availableCount,
                    ]
                )
            );
            break;

        case 'available_keys_count':
            httpResponse(
                200,
                json_encode(
                    [
                        'response_date' => time(),
                        'available_keys_count' => availableKeysCount($pdo),
                    ]
                )
            );
            break;

        case 'insertkey':
            $binaryKeyString = $_GET['key'];
            $checksum = $_GET['checksum'];

            insertNewKeyFromRequest($pdo, $binaryKeyString, $checksum);
            break;
    }
} catch (PDOException $e) {
    echo $e->getMessage();
} catch (Throwable $e) {
    echo $e->getMessage();
}


function httpAuth() {
    $headers = getallheaders();

    if (!isset($headers['Authorization'])) {
        errorResponse(401, 'Authorization required!');
    } else {
        $authHeader = $headers['Authorization'];
        preg_match('/Bearer: (.*)/i', $authHeader, $matches);

        if (!isset($matches[1])) {
            errorResponse(401, 'Authorization required!');
        }

        if ($matches[1] === AUTH_TOKEN_ADMIN) {
            $authType = AUTH_TYPE_ADMIN;
        } elseif ($matches[1] === AUTH_TOKEN_USER) {
            $authType = AUTH_TYPE_USER;
        } else {
            errorResponse(401, 'Authorization required');
        }
    }
}

function requestAndSaveNewKey(\PDO $pdo) {
    $curl = curl_init();
    curl_setopt($curl, CURLOPT_URL, KEY_SERVER_URI);
    curl_setopt($curl, CURLOPT_RETURNTRANSFER, true);
    curl_setopt($curl, CURLOPT_TIMEOUT, CURL_CONNECTION_TIMEOUT);
    curl_setopt($curl, CURLOPT_HTTPHEADER, [
        'Authorization' => sprintf('Bearer: %s', KEY_SERVER_AUTH_TOKEN),
    ]);
    curl_setopt($curl, CURLOPT_POST, true);
    curl_setopt($curl, CURLOPT_POSTFIELDS, sprintf('key=%s', KEY_SERVER_POST_AUTH_TOKEN));

    $content = curl_exec($curl);
    $content = trim($content);

    if ($content && strlen($content) === KEY_LENGTH) {
        $crc32 = crc32($content);

        insertNewKey($pdo, $content, $crc32);
    }

    curl_close($curl);
}

function httpResponse(int $code, string $message) {
    if ($code < 100 || $code > 599) {
        $code = 500;
        $message = 'Invalid status code';
    }

    http_response_code($code);
    die($message);
}

function errorResponse(int $code, string $message, $data = []) {
    httpResponse(
        $code,
        json_encode(
            [
                'message' => $message,
                'data' => $data,
            ]
        )
    );
}

function initPdo() {
    $pdo = new \PDO(sprintf('sqlite:%s', SQLITE_DB_PATH));

    if (!$pdo) {
        errorResponse(500, 'Cannot connect to DB');
    }

    return $pdo;
}

function query(\PDO $pdo, string $query) {
    $result = [];
    $statement = $pdo->query($query);

    while ($row = $statement->fetch(\PDO::FETCH_ASSOC)) {
        $result[] = $row;
    }

    return $result;
}

function insertNewKeyFromRequest(\PDO $pdo, string $binaryKeyString, string $checksum) {
    global $authType;

    if ($authType !== AUTH_TYPE_ADMIN) {
        errorResponse(403, 'Forbidden. Method is available only for keygen server');
    }

    if (crc32($binaryKeyString) != $checksum) {
        errorResponse(400, sprintf('Bad CRC32. Expected %s, given %s', crc32($binaryKeyString), $checksum));
    }

    $result = insertNewKey($pdo, $binaryKeyString, $checksum);

    if (!is_numeric($result)) {
        errorResponse(500, 'Error at insert data', $result);
    }

    return $result;
}

function insertNewKey(\PDO $pdo, string $binaryKeyString, string $checksum) {
    $statement = $pdo->prepare(
        'INSERT INTO keys(created_at, keyblobcontent, checksum) VALUES(:created_at, :keyblobcontent, :checksum)'
    );

    $result = $statement->execute([
        ':created_at' => time(),
        ':keyblobcontent' => $binaryKeyString,
        ':checksum' => $checksum,
    ]);

    if (false === $result) {
        return $statement->errorInfo();
    }

    return $pdo->lastInsertId();
}

function nextKey(\PDO $pdo) {
    $pdo->beginTransaction();

    try {
        $key = query($pdo, 'SELECT * FROM keys WHERE used_at IS NULL LIMIT 1;');
        $lastUsed = query($pdo, 'SELECT MAX(used_at) AS last_used_at FROM keys;');

        if (isset($lastUsed[0]['last_used_at'])) {
            $lastUsed = (int)$lastUsed[0]['last_used_at'];

            if (time() < $lastUsed + 10) {
                errorResponse(400, 'You cannot ask for new code more than once per 10 sec');
            }
        }

        if (isset($key[0])) {
            $statement = $pdo->prepare(
                'UPDATE keys SET used_at = :used_at WHERE id = :id;'
            );

            $statement->bindValue(':used_at', time());
            $statement->bindValue(':id', (int) $key[0]['id']);

            $statement->execute();

            if ('00000' !== $statement->errorCode()) {
                $pdo->rollBack();
                errorResponse(500, json_encode($statement->errorInfo()));
                die;
            }
        }

        $pdo->commit();

        return $key[0];
    } catch (Exception $e) {
        $pdo->rollBack();
    }
}

function availableKeysCount(\PDO $pdo) {
    $keys = query($pdo, 'SELECT COUNT(*) AS available_keys_count FROM keys WHERE used_at IS NULL;');

    if (isset($keys[0]['available_keys_count'])) {
        return (int) $keys[0]['available_keys_count'];
    }

    return 0;
}

function gc(\PDO $pdo) {
    query($pdo, 'DELETE FROM keys WHERE used_at IS NOT NULL;');
}
