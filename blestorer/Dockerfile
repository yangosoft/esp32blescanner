# Use official Node.js LTS image
FROM node:24

# Set working directory
WORKDIR /usr/src/app

# Copy package.json and package-lock.json
COPY package*.json ./

# Install dependencies
RUN npm install

# Copy application code
COPY . .

# Create output directory
RUN mkdir -p /usr/src/app/output

# Expose port
EXPOSE 4444

# Set environment variable for output directory (optional)
ENV OUTPUT_DIR=/usr/src/app/output

# Start the app, redirect logs to a file and stdout
CMD ["sh", "-c", "node main.js | tee /usr/src/app/output/server.log"]
