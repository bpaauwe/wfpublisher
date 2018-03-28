# wfpublisher
Monitor UDP port 50222 for WeatherFlow weather data and publish to various weather services.
<p>
Publishing is controlled by a JSON formatted configuration file. Each service is listed along with configuration
information for the service. Each one can be enabled or disabled.  
<p>
The weather data is formatted to match the service specifications. If a service has restrictions on how often
data can be sent, multiple incoming data packets will be averaged before sending to the service. Sending to
each service happens on a separate thread which prevents slow network response or a down service from impacting
the publishing to other services.
<p>
The following servcies are currently supported:
<p>
<h2>logfile</h2> 
       Write the weather data to a file. The file is appended with a line containing the weather data separated
       with '|' characters.
<p>      
<h2>display</h2> 
       Displays the weather data on the terminal screen. Each weather update re-writes the terminal display. This
       is useful for monitoring or debugging.
<p>      
<h2>mysql</h2> 
       Writes the weather data to a MariaDB/MYSQL database. The current code is more of a proof of concept and 
       would need to be modified for any specific application.
<p>       
<h2>MQTT</h2> 
       Send the weather data to a mqtt broker. Each weather value is sent as a separate message.
<p>       
<h2>Weather Underground</h2> 
       Publish the data to a Weather Underground personal weather station.
<p>       
<h2>Weather Bug</h2> 
       Publish the data to a Weather Bug backyard weather station.
<p>       
<h2>CWOP</h2> 
       Publsih the weather data to the Citizens Weather Observation Program service.
<p>       
<h2>Personal Weather Station</h2> 
       Publish the weather data to pwsweather.com.<br>
<p>       

