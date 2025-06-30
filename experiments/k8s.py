import os
import json
from itertools import product
import subprocess
import time

# ==== CONFIGURATION ====



algos = ["S3FIFO", "ARC"]
params_list = [
    {"fifo_size_ratio": 0.1, "move_to_main_threshold": 2},
    {"fifo_size_ratio": 0.2, "move_to_main_threshold": 3},
]
image = "myrepo/cachesim:latest"  # Replace with your image
volume_path = "/mnt/moofs"

# ==== GENERATE JOB YAML ====
os.makedirs("jobs", exist_ok=True)
job_template = """
apiVersion: batch/v1
kind: Job
metadata:
  name: simulate-{job_id}
spec:
  template:
    spec:
      containers:
      - name: simulator
        image: {image}
        command:
          - "python"
          - "simulate.py"
          - "--trace"
          - "{trace}"
          - "--algo"
          - "{algo}"
          - "--params"
          - '{params}'
        volumeMounts:
        - name: moofs
          mountPath: /mnt/moofs
      restartPolicy: Never
      volumes:
      - name: moofs
        hostPath:
          path: {volume_path}
          type: Directory
  backoffLimit: 1
"""

job_map = []  # List of (job_id, job_name)
job_id = 0
for trace, algo, params in product(traces, algos, params_list):
    job_name = f"simulate-{job_id}"
    yaml = job_template.format(
        job_id=job_id,
        trace=trace,
        algo=algo,
        params=json.dumps(params),
        image=image,
        volume_path=volume_path,
    )
    with open(f"jobs/job-{job_id}.yaml", "w") as f:
        f.write(yaml)
    job_map.append((job_id, job_name))
    job_id += 1

# ==== APPLY JOBS ====
for job_id, job_name in job_map:
    subprocess.run(["kubectl", "apply", "-f", f"jobs/job-{job_id}.yaml"])

# ==== WAIT FOR COMPLETION ====
def all_jobs_done():
    result = subprocess.run(["kubectl", "get", "jobs", "-o", "json"], capture_output=True, text=True)
    jobs = json.loads(result.stdout)["items"]
    completed = sum(1 for job in jobs if job["status"].get("succeeded") == 1)
    return completed == len(job_map)

print("Waiting for jobs to complete...")
while not all_jobs_done():
    time.sleep(5)

# ==== GATHER LOGS ====
results = []
for job_id, job_name in job_map:
    pod_name = subprocess.run(
        ["kubectl", "get", "pods", "--selector=job-name=" + job_name, "-o", "jsonpath={.items[0].metadata.name}"],
        capture_output=True, text=True).stdout.strip()

    log = subprocess.run(["kubectl", "logs", pod_name], capture_output=True, text=True).stdout.strip()
    print(f"[{job_name}] {log}")
    if "Miss ratio:" in log:
        try:
            ratio = float(log.strip().split()[-1])
            results.append((job_name, ratio))
        except:
            results.append((job_name, None))

print("\nAll gathered results:")
for job_name, ratio in results:
    print(f"{job_name}: {ratio}")
