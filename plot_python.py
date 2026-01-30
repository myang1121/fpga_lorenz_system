import matplotlib.pyplot as plt

dt = (1./256)
x = [-1.]
y = [0.1]
z = [25.]
sigma = 10.0
beta = 8./3.
rho = 28.0

def dx(sigma, x, y):
    return sigma*(y-x)

def dy(rho, x, y, z):
    return x*(rho-z)-y

def dz(beta, x, y, z):
    return x*y - beta*z

for i in range(10000):
    x.extend([x[i] + dt*dx(sigma, x[i], y[i])])
    y.extend([y[i] + dt*dy(rho, x[i], y[i], z[i])])
    z.extend([z[i] + dt*dz(beta, x[i], y[i], z[i])])

# Print first 100 values
print("First 100 X values:")
for i in range(100):
    print(f"x[{i}] = {x[i]}")

print("\nFirst 100 Y values:")
for i in range(100):
    print(f"y[{i}] = {y[i]}")

print("\nFirst 100 Z values:")
for i in range(100):
    print(f"z[{i}] = {z[i]}")

# Plot curve
#2D
t = [i * dt for i in range(len(x))]

fig, axes = plt.subplots(3, 1, figsize=(10, 8), sharex=True)

axes[0].plot(t, x, lw=0.5)
axes[0].set_ylabel('X')
axes[0].set_title('Lorenz System 2D, state variables vs t')

axes[1].plot(t, y, lw=0.5)
axes[1].set_ylabel('Y')

axes[2].plot(t, z, lw=0.5)
axes[2].set_ylabel('Z')
axes[2].set_xlabel('Time')

plt.tight_layout()
plt.show()

#3D
fig = plt.figure(figsize=(10, 8))
ax = fig.add_subplot(111, projection='3d')
ax.plot(x, y, z, lw=0.5)
ax.set_xlabel('X')
ax.set_ylabel('Y')
ax.set_zlabel('Z')
ax.set_title('Lorenz System 3D, state variables vs t')
plt.show()

# plot modelSim solution vector plot
