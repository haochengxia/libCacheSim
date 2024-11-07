import matplotlib.pyplot as plt
import numpy as np
from scipy.stats import gaussian_kde

file_name = "example"
with open(file_name) as file:
    context = file.readlines()
    hit_pos = []
    for l in context[:-1]:
        hit_pos.append(int(l.split(" ")[-1].strip()))

    fifo_size_ratio = float(context[-1].split(" ")[1].split("-")[1])
    ghost_ratio = 1 - fifo_size_ratio
    cache_size = int(context[-1].split(",")[0].split(" ")[-1])
    # Given hit positions as integers
    hit_positions = hit_pos

    # Normalization factor (change this value as needed)
    normalization_factor = int(cache_size * ghost_ratio)  # Example value

    # Normalize hit positions
    normalized_positions = [pos / normalization_factor for pos in hit_positions]

    # Generate a larger sample around these values
    large_sample = np.random.choice(normalized_positions, size=10000, replace=True)

    # Create a Gaussian KDE for the probability density function
    kde = gaussian_kde(large_sample)

    # Define positions for PDF plotting
    positions = np.linspace(min(large_sample) - 0.1, max(large_sample) + 0.1, 1000)
    pdf_values = kde(positions)

    # Plotting the PDF
    plt.figure(figsize=(10, 6))
    plt.plot(positions, pdf_values, label="Normalized Hit Position PDF")
    plt.title("Probability Density Function (PDF) of Normalized Hit Positions")
    plt.xlabel("Normalized Hit Position")
    plt.ylabel("Probability Density")
    plt.legend()
    plt.grid(True)
    plt.savefig(file_name + "_pdf.png")
