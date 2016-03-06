<?php
@set_time_limit(0);
@ini_set('max_execution_time',0);
@ini_set('output_buffering',0);

define('TEST_FILE',"100.csv"); //100 domains
define('FILE_DOMAINS', 100);
define('ABSOLUTE_PATH',dirname(__FILE__));

function ex($cfe) {
$res = '';
 if (!empty($cfe))
  {
  if(function_exists('exec')){ @exec($cfe,$res);$res = join("\n",$res); }
  elseif(function_exists('shell_exec')){ $res = @shell_exec($cfe); }
  elseif(function_exists('system')){ @ob_start();@system($cfe);$res = @ob_get_contents();@ob_end_clean(); }
  elseif(function_exists('passthru')){ @ob_start();@passthru($cfe);$res = @ob_get_contents();@ob_end_clean(); }
  elseif(@is_resource($f = @popen($cfe,"r"))){ $res = "";while(!@feof($f)) { $res .= @fread($f,1024); }@pclose($f); }
  } 
return $res; }

function to_int($val) {
  return (int)$val;
}


$time = array(array('pending_request','time'));

$inc = 10;
$count_try = 3; // делаем N запусков с одним параметром что бы вычислить среднее арифметические значение

for($pending_request = $inc; $pending_request <= FILE_DOMAINS; $pending_request += $inc ) {

  $summ = 0;
  for($try = 0; $try < $count_try; $try++) {
    $res = ex(ABSOLUTE_PATH.'/dist/Debug/GNU-Linux/crawler_github -n'.$pending_request.' '.TEST_FILE.' 2>/dev/null');

    if(preg_match_all("/\w:\s(\d+);?/", $res, $match)) {
      $summ += $match[1][5];
    }
  }



  $time[] = array((int)$pending_request, $summ/$count_try);
}
?>
  <html>
  <head>
    <title>Gar|k crawler benckmark</title>
    <script type="text/javascript" src="https://www.gstatic.com/charts/loader.js"></script>
    <script type="text/javascript">
      google.charts.load('current', {'packages':['corechart']});
      google.charts.setOnLoadCallback(drawChart);

      function drawChart() {
        var time_data = google.visualization.arrayToDataTable(<?php echo json_encode($time);?>);

        var options = {
          title: 'request & time (in milliseconds)',
          legend: { position: 'bottom' }
        };

        var chart = new google.visualization.LineChart(document.getElementById('time_chart'));
        chart.draw(time_data, options);

      }
    </script>
  </head>
  <body>
    <div id="time_chart" style="width: 900px; height: 500px"></div>
  </body>
</html>