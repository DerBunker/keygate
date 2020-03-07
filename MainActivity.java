package com.example.keygate;

import android.bluetooth.BluetoothAdapter;
import android.bluetooth.BluetoothDevice;
import android.bluetooth.BluetoothSocket;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.os.Bundle;
import android.os.StrictMode;
import android.text.method.ScrollingMovementMethod;
import android.view.View;
import android.widget.AdapterView;
import android.widget.ArrayAdapter;
import android.widget.Button;
import android.widget.ListView;
import android.widget.TextView;
import android.widget.Toast;

import androidx.appcompat.app.AppCompatActivity;

import com.google.gson.Gson;

import org.apache.commons.io.IOUtils;

import java.io.InputStream;
import java.io.InputStreamReader;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.util.ArrayList;
import java.util.Set;
import java.util.UUID;

public class MainActivity extends AppCompatActivity {
    Button openCloseButton;
    TextView activityLogTextView;
    TextView operationIndicator;
    ListView pairedListView;

    BluetoothAdapter bluetoothAdapter;
    BluetoothDevice keyTransmitterDevice;
    BluetoothSocket bluetoothSocket;
    ArrayList<String> pairedDeviceArrayList;
    ArrayAdapter<String> pairedDeviceAdapter;

    private UUID myUUID;

    private String keyServerAuthKey = "##AUTH_TOKEN_USER##";
    private String keyServerUri = "http://##YOUR_KEY##/index.php?action=getkey";
    private Integer buttonDisableTimeout = 3;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        StrictMode.ThreadPolicy policy = new StrictMode.ThreadPolicy.Builder().permitAll().build();

        StrictMode.setThreadPolicy(policy);

        setContentView(R.layout.activity_main);
        final String UUID_STRING_WELL_KNOWN_SPP = "00001101-0000-1000-8000-00805F9B34FB";

        if (!getPackageManager().hasSystemFeature(PackageManager.FEATURE_BLUETOOTH)) {
            Toast.makeText(this, "Bluetooth not supported", Toast.LENGTH_LONG).show();
            finish();
            return;
        }

        myUUID = UUID.fromString(UUID_STRING_WELL_KNOWN_SPP);

        openCloseButton = (Button) findViewById(R.id.openCloseButton);
        operationIndicator = (TextView) findViewById(R.id.operationIndicator);
        activityLogTextView = (TextView) findViewById(R.id.activityLogTextView);
        activityLogTextView.setMovementMethod(new ScrollingMovementMethod());
        pairedListView = (ListView) findViewById(R.id.pairedListView);

        openCloseButton.setEnabled(false);

        openCloseButton.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                try {
                    OutputStream outputStream = bluetoothSocket.getOutputStream();
                    InputStream inputStream = bluetoothSocket.getInputStream();

                    String keyData = requestNewKey();

                    if (keyData.length() > 0) {
                        activityLogTextView.append("Send keydata to bluetooth transmitter. \n" + keyData + "\n");
                        outputStream.write(keyData.getBytes());
                    } else {
                        activityLogTextView.append(
                                "Empty response from keyserver\n"
                        );
                    }

                    openCloseButton.setEnabled(false);

                    openCloseButton.postDelayed(new Runnable() {
                        @Override
                        public void run() {
                            openCloseButton.setEnabled(true);
                        }
                    }, buttonDisableTimeout * 1000);
                } catch (Exception e) {
                    Toast.makeText(getApplicationContext(), e.getMessage(), Toast.LENGTH_LONG).show();
                }
            }
        });
    }

    @Override
    protected void onStart() {
        super.onStart();

        bluetoothAdapter = BluetoothAdapter.getDefaultAdapter();
        activityLogTextView.append(
                "This device: " + bluetoothAdapter.getName() + " " + bluetoothAdapter.getAddress() + "\n"
        );

        if (!bluetoothAdapter.isEnabled()) {
            Intent enableIntent = new Intent(BluetoothAdapter.ACTION_REQUEST_ENABLE);
            startActivityForResult(enableIntent, 1);
        }

        setup();
    }

    private void setup() {
        Set<BluetoothDevice> pairedDevices = bluetoothAdapter.getBondedDevices();

        if (pairedDevices.size() > 0) {
            pairedDeviceArrayList = new ArrayList<>();

            for (BluetoothDevice device : pairedDevices) {
                pairedDeviceArrayList.add(device.getName() + "\n" + device.getAddress());
            }

            pairedDeviceAdapter = new ArrayAdapter<String>(this, android.R.layout.simple_list_item_1, pairedDeviceArrayList);
            pairedListView.setAdapter(pairedDeviceAdapter);

            pairedListView.setOnItemClickListener(new AdapterView.OnItemClickListener() {
                @Override
                public void onItemClick(AdapterView<?> parent, View view, int position, long id) {
                    String itemValue = (String) pairedListView.getItemAtPosition(position);
                    String MAC = itemValue.substring(itemValue.length() - 17);

                    activityLogTextView.append(
                            "Trying to pair with selected device. " + MAC + " \n"
                    );

                    try {
                        keyTransmitterDevice = bluetoothAdapter.getRemoteDevice(MAC);
                        bluetoothSocket = keyTransmitterDevice.createRfcommSocketToServiceRecord(myUUID);

                        bluetoothSocket.connect();

                        if (bluetoothSocket.isConnected()) {
                            openCloseButton.setEnabled(true);

                            BtInputThread btInputThread = new BtInputThread(bluetoothSocket.getInputStream());
                            btInputThread.start();
                        }
                    } catch (Exception e) {
                        Toast.makeText(getApplicationContext(), e.getMessage(), Toast.LENGTH_LONG).show();
                    }
                }
            });
        }

    }

    private String requestNewKey() {
        try {
            URL keyserverEndpoint = new URL(keyServerUri + "&_=" + System.currentTimeMillis());
            HttpURLConnection keyserverConnection = (HttpURLConnection) keyserverEndpoint.openConnection();
            keyserverConnection.setRequestProperty("Authorization", "Bearer: " + keyServerAuthKey);
            keyserverConnection.setRequestMethod("GET");

            if (keyserverConnection.getResponseCode() == 200) {
                InputStreamReader responseReader = new InputStreamReader(keyserverConnection.getInputStream());
                String json = IOUtils.toString(responseReader);

                Gson gson = new Gson();
                KeyPayload keyPayload = gson.fromJson(json, KeyPayload.class);

                activityLogTextView.append(
                        "Available keys on server: " + keyPayload.available_keys_count.toString() + "\n"
                );

                return keyPayload.key_blob + ":" + calculateRiterShittyChecksum(keyPayload.key_blob) + "\r\n";
            } else {
                Toast.makeText(getApplicationContext(), "BAD RESPONSE: " + keyserverConnection.getResponseMessage(), Toast.LENGTH_LONG).show();
            }

            keyserverConnection.disconnect();
        } catch (Exception e) {
            Toast.makeText(getApplicationContext(), e.getMessage(), Toast.LENGTH_LONG).show();
        }

        return "";
    }

    private String calculateRiterShittyChecksum(String keyBlob) {
        Integer count = 0;

        for (char el : keyBlob.toCharArray()) {
            if (el == '1') {
                count++;
            }
        }

        return count.toString();
    }


    private class KeyPayload {
        public Integer response_date;
        public String created_at;
        public String key_blob;
        public String checksum;
        public Integer available_keys_count;

        public KeyPayload(Integer response_date, String created_at, String key_blob, String checksum, Integer available_keys_count) {
            this.response_date = response_date;
            this.created_at = created_at;
            this.key_blob = key_blob;
            this.checksum = checksum;
            this.available_keys_count = available_keys_count;
        }
    }

    private class BtInputThread extends Thread {
        private InputStream inputStream;
        private String sbprint = "";

        public BtInputThread(InputStream inputStream) {
            this.inputStream = inputStream;
        }

        @Override
        public void run() {
            try {
                StringBuilder sb = new StringBuilder();

                while (true) {
                    byte[] buffer = new byte[1];
                    int bytes = inputStream.read(buffer);
                    String strIncom = new String(buffer, 0, bytes);
                    sb.append(strIncom);
                    int endOfLineIndex = sb.indexOf("\r\n");

                    if (endOfLineIndex > 0) {
                        sbprint = sb.substring(0, endOfLineIndex);
                        sb.delete(0, sb.length());

                        activityLogTextView.append(
                                "Answer from device: " + sbprint + "\n"
                        );
                    }
                }
            } catch (Exception e) {
                activityLogTextView.append(
                        "Read from BT exception: " + e.getMessage() + "\n"
                );
            }
        }
    }
}
