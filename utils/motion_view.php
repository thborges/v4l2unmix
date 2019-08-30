<?php
$dir    = 'cams';

echo "<html>
	<head>
		<style>
			body * { padding: 2px; vertical-align: top;}
			#divvideo { boder: 1px solid; display: none; }
		</style>
		<script>
		function hidevideo() {
			document.getElementById('divvideo').style.display = 'none'; 
		}
		function showvideo(div, v) {
			var div = document.getElementById(div);
			if (div.style.display == 'none') {
				var video = document.createElement('video');
				video.setAttribute('src', v);
				video.controls = true;
				div.appendChild(video);
				div.style.display = 'inline-block';
				video.play();
			} else {
				div.style.display = 'none';
				div.innerHTML = '';
			}
		}
		</script>
	<body\r";

if ($_GET['day'] != null && $_GET['monitor'] != null) {
	$m = $_GET['monitor'];
	$d = $_GET['day'];
	$events = glob("./$dir/$m/$d/*.jpg");
	$e = end($events);
	do {
		if ($e == '.' || $e == '..') continue;
		#echo "<a target='popup' href='./$dir/$m/$d/$e/$e-video.mp4'><img width='200px' src='./$dir/$m/$d/$e/snapshot.jpg'></img></a>";
		$v = substr_replace($e, "mp4", -3);
		echo "<img id='$e' width='200px' src='$e'
			onclick='showvideo(\"v$e\", \"$v\")'></img><div id='v$e' style='display: none;'></div>";

	} while ($e = prev($events));
}
else {
	echo "<a href='/cam1'> <img src='/cam1' border=0 width=25%></a>
	      <a href='/cam2'> <img src='/cam2' border=0 width=25%></a>";

	$monitors = scandir($dir);
	foreach ($monitors as $m) {
		if ($m == '.' || $m == '..') continue;
		echo "<h1>Events for $m</h1>";

		$days = scandir($dir . "/$m", 1);
		foreach ($days as $d) {
			if ($d == '.' || $d == '..') continue;
			echo "<a href='index.php?monitor=$m&day=$d'>$d</a></br>";
		}
	}
}

echo "</body></html>";
?>
