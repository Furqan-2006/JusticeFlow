function loadOSMetrics() {
    fetch('/api/os_metrics')
        .then(res => res.json())
        .then(data => {
            // Error or backend unavailable
            if (data.status === "error") {
                document.getElementById("metrics-status").innerText = data.message || "Unknown error.";
                return;
            } else {
                document.getElementById("metrics-status").innerText = "";
            }

            // Example metric shape -- adapt as needed
            // {
            //   "active_sessions": 4,
            //   "thread_pool": {"total": 8, "busy": 3},
            //   ... (other fields)
            // }

            const sessions = data.active_sessions || 0;
            const poolTotal = data.thread_pool?.total || 0;
            const poolBusy = data.thread_pool?.busy || 0;

            const ctx = document.getElementById('osMetricsChart').getContext('2d');
            // Destroy previous chart if it exists
            if (window.osChart) window.osChart.destroy();

            window.osChart = new Chart(ctx, {
                type: 'bar',
                data: {
                    labels: ['Active Sessions', 'Thread Pool Total', 'Thread Pool Busy'],
                    datasets: [{
                        label: 'OS Metrics',
                        data: [sessions, poolTotal, poolBusy],
                        backgroundColor: ['#4287f5', '#34a853', '#ea4335']
                    }]
                },
                options: {
                    responsive: false,
                    plugins: {
                        legend: { display: false }
                    }
                }
            });
        })
        .catch(err => {
            document.getElementById("metrics-status").innerText = "Failed to load metrics: " + err;
        });
}

// Initial load and polling every 5 seconds
loadOSMetrics();
setInterval(loadOSMetrics, 5000);