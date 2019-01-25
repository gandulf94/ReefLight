<!DOCTYPE html>
<html>
<head>
<script>
window.onload = function() {

var chart = new CanvasJS.Chart("chartContainer", {       
	animationEnabled: true,
	title: {
		text: "Try Dragging Any Column to Resize"
	},
	axisX:{
		minimum: 5,
		maximum: 95
	},
	data: [{
		type: "column",
		dataPoints: [
			{ x: 10, y: 71 },
			{ x: 20, y: 55 },
			{ x: 30, y: 50 },
			{ x: 40, y: 65 },
			{ x: 50, y: 95 },
			{ x: 60, y: 68 },
			{ x: 70, y: 28 },
			{ x: 80, y: 34 },
			{ x: 90, y: 14 }
		]
	}]
});
chart.render();

var xSnapDistance = chart.axisX[0].convertPixelToValue(chart.get("dataPointWidth")) / 2;
var ySnapDistance = 3;

var xValue, yValue;

var mouseDown = false;
var selected = null;
var changeCursor = false;

var timerId = null;

function getPosition(e) {
	var parentOffset = $("#chartContainer > .canvasjs-chart-container").offset();          	
	var relX = e.pageX - parentOffset.left;
	var relY = e.pageY - parentOffset.top;
	xValue = Math.round(chart.axisX[0].convertPixelToValue(relX));
	yValue = Math.round(chart.axisY[0].convertPixelToValue(relY));
}

function searchDataPoint() {
	var dps = chart.data[0].dataPoints;
	for(var i = 0; i < dps.length; i++ ) {
		if( (xValue >= dps[i].x - xSnapDistance && xValue <= dps[i].x + xSnapDistance) && (yValue >= dps[i].y - ySnapDistance && yValue <= dps[i].y + ySnapDistance) ) 
		{
			if(mouseDown) {
				selected = i;
				break;
			} else {
				changeCursor = true;
				break; 
			}
		} else {
			selected = null;
			changeCursor = false;
		}
	}
}

jQuery("#chartContainer > .canvasjs-chart-container").on({
	mousedown: function(e) {
		mouseDown = true;
		getPosition(e);  
		searchDataPoint();
	},
	mousemove: function(e) {
		getPosition(e);
		if(mouseDown) {
			clearTimeout(timerId);
			timerId = setTimeout(function(){
				if(selected != null) {
					chart.data[0].dataPoints[selected].y = yValue;
					chart.render();
				}   
			}, 0);
		}
		else {
			searchDataPoint();
			if(changeCursor) {
				chart.data[0].set("cursor", "n-resize");
			} else {
				chart.data[0].set("cursor", "default");
			}
		}
	},
	mouseup: function(e) {
		if(selected != null) {
			chart.data[0].dataPoints[selected].y = yValue;
			chart.render();
			mouseDown = false;
		}
	}
});

}
</script>
</head>
<body>    
<div id="chartContainer" style="height: 300px; width: 100%;"></div>  
<script src="https://canvasjs.com/assets/script/canvasjs.min.js"></script>
<script src="https://code.jquery.com/jquery-3.1.1.min.js"></script>  
</body>
</html>
